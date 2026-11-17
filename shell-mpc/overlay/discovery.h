// SynthyBoy - MPC overlay: run-time discovery of the display. No model of the MPC is written into the code: the CRTC and the primary plane the application uses, the free overlay planes
// that can carry the overlay, the panel size and the refresh rate are found from what the display driver reports (a DrmSnapshot, made by drm_linux.h on the device and by the tests
// out of the tables of both devices), and the cores the overlay must stay off are found from the names and affinities of the application's own threads.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Rules (docs/MPC.md "Overlay (experimental)", "Discovery"):
//  CRTC / primary plane: the PRIMARY plane (type 1) that is on a CRTC with a valid mode and shows a framebuffer is the application's UI plane, its CRTC is ours. A driver that reports no
//  plane types (no universal planes): the CRTC with a valid mode and a framebuffer (GETCRTC), and the plane showing that framebuffer.
//  Overlay plane: free (no CRTC, no framebuffer), can go on that CRTC (possible_crtcs), offers ARGB8888, is not a cursor plane, and stacks above every plane the application has on the CRTC
//  (zpos, when the driver has the property: the plane's zpos at rest must be above theirs; without zpos an OVERLAY type plane stacks above the primary one by the hardware order).
//  The lowest numbered one is preferred; the others are the spares for a plane another program takes later.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace synthyboy_mpc { namespace overlay {

constexpr uint32_t kFourccArgb8888 = 0x34325241u;   // 'AR24' (DRM_FORMAT_ARGB8888)

enum PlaneType { kPlaneTypeUnknown = -1, kPlaneTypeOverlay = 0, kPlaneTypePrimary = 1, kPlaneTypeCursor = 2 };

struct PlaneInfo
{
    uint32_t id = 0;
    uint32_t possibleCrtcs = 0;      // bit mask of CRTC indices
    uint32_t crtcId = 0;             // 0: not on any CRTC now
    uint32_t fbId = 0;               // 0: no framebuffer
    int type = kPlaneTypeUnknown;    // the "type" property
    bool hasZpos = false;
    uint64_t zpos = 0;               // the "zpos" property (the value at rest / of the current state)
    bool zposMutable = false;        // the property is a range that may be written (not immutable): the XL's planes 0 .. 7; the Key 61 has no zpos property at all
    uint64_t zposMin = 0, zposMax = 0;   // the range, when zposMutable
    uint32_t zposProp = 0;           // the id of the property on this plane (for OBJ_GETPROPERTIES / OBJ_SETPROPERTY)
    std::vector<uint32_t> formats;
    bool hasArgb() const { return std::find (formats.begin(), formats.end(), kFourccArgb8888) != formats.end(); }
};

struct CrtcInfo
{
    uint32_t id = 0;
    int index = 0;                   // position in the resource list (the bit in possible_crtcs, the pipe of the vblank calls)
    bool modeValid = false;
    int width = 0, height = 0;       // the mode (hdisplay x vdisplay)
    int vrefresh = 0;                // what the mode says (the real rate is measured)
    uint32_t fbId = 0;               // the framebuffer the CRTC's primary plane shows (GETCRTC)
};

struct DrmSnapshot
{
    std::vector<CrtcInfo> crtcs;
    std::vector<PlaneInfo> planes;
};

struct DisplayInfo
{
    bool ok = false;
    std::string error;               // when ! ok: why the overlay cannot run here
    std::string device;              // /dev/dri/cardN
    uint32_t crtc = 0;
    int crtcIndex = 0;
    int panelW = 0, panelH = 0;
    int modeRefresh = 0;             // the mode's own number (not trusted)
    uint32_t primaryPlane = 0;       // the application's UI plane (0: not told by the driver)
    std::vector<uint32_t> appPlanes; // every plane the application has on the CRTC
    std::vector<uint32_t> candidates;// free overlay planes, the one to use first
    size_t current = 0;              // index into candidates
    std::string how;                 // how the CRTC was found: "primary plane" | "crtc framebuffer"
    uint32_t plane() const { return current < candidates.size() ? candidates[current] : 0; }
};

/** The rules above. `info.device` is set by the caller. */
inline DisplayInfo discoverDisplay (const DrmSnapshot& snap)
{
    DisplayInfo d;
    const CrtcInfo* crtc = nullptr;
    const PlaneInfo* primary = nullptr;
    for (const PlaneInfo& p : snap.planes)
        if (p.type == kPlaneTypePrimary && p.crtcId != 0 && p.fbId != 0)
            for (const CrtcInfo& c : snap.crtcs)
                if (c.id == p.crtcId && c.modeValid && ! primary)
                {
                    primary = &p;
                    crtc = &c;
                    d.how = "primary plane";
                }
    if (! crtc)
    {
        for (const CrtcInfo& c : snap.crtcs)
            if (c.modeValid && c.fbId != 0 && ! crtc)
            {
                crtc = &c;
                d.how = "crtc framebuffer";
            }
        if (crtc)
            for (const PlaneInfo& p : snap.planes)
                if (p.crtcId == crtc->id && p.fbId == crtc->fbId && ! primary)
                    primary = &p;
    }
    if (! crtc)
    {
        d.error = "no CRTC with a mode and a framebuffer (the application's display was not found)";
        return d;
    }
    d.crtc = crtc->id;
    d.crtcIndex = crtc->index;
    d.panelW = crtc->width;
    d.panelH = crtc->height;
    d.modeRefresh = crtc->vrefresh;
    d.primaryPlane = primary ? primary->id : 0;
    uint64_t appTop = 0;
    bool appZpos = true, any = false;
    for (const PlaneInfo& p : snap.planes)
        if (p.crtcId == crtc->id && p.fbId != 0)
        {
            d.appPlanes.push_back (p.id);
            any = true;
            if (p.hasZpos)
                appTop = std::max (appTop, p.zpos);
            else
                appZpos = false;
        }
    (void) any;
    for (const PlaneInfo& p : snap.planes)
    {
        if (p.crtcId != 0 || p.fbId != 0)                           // in use (also: the application's own planes)
            continue;
        if (primary && p.id == primary->id)
            continue;
        if (p.type == kPlaneTypeCursor || p.type == kPlaneTypePrimary)
            continue;
        if (! (p.possibleCrtcs & (1u << crtc->index)))
            continue;
        if (! p.hasArgb())
            continue;
        if (p.hasZpos && ! p.zposMutable && appZpos && p.zpos <= appTop && ! d.appPlanes.empty())   // it would go UNDER a plane of the application (a plane whose zpos can be written gets the order from the stacking rule when it is taken: its value now, a leftover of an earlier run, says nothing)
            continue;
        d.candidates.push_back (p.id);
    }
    std::sort (d.candidates.begin(), d.candidates.end());
    if (d.candidates.empty())
    {
        d.error = "no free overlay plane on CRTC " + std::to_string (crtc->id) + " that offers ARGB8888 and stacks above the application's planes";
        return d;
    }
    d.ok = true;
    return d;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Stacking: the host keeps its mouse pointer on a small plane of its own. Measured on the MPC XL (2026-11-16): planes 34 (the host's picture, zpos 0), 40 (the host's pointer, 64x64, type Overlay,
// zpos 1) and 46 (ours, zpos 2); every plane has a "zpos" property that is a mutable range 0 .. 7. With ours at 2 the pointer was hidden UNDER our picture; with our zpos set to 0 the kernel's
// normalised order was host 0, ours 1, pointer 2 (planes with equal zpos are ordered by plane object id, the lower id below) and the pointer was visible on top. On the Key 61 no plane has a zpos
// property and the pointer is on top by the hardware's own order.

/** A plane of the application on our CRTC with what the order needs of it. */
struct StackPlane
{
    uint32_t id = 0;
    uint64_t zpos = 0;
};

/** The kernel's order of two planes on one CRTC: by zpos, equal zpos by plane id (the lower id below). */
inline bool stackedAbove (uint64_t zposA, uint32_t idA, uint64_t zposB, uint32_t idB) noexcept
{
    return zposA > zposB || (zposA == zposB && idA > idB);
}

struct ZposChoice
{
    bool possible = false;           // a value exists that puts our plane directly above the primary plane and below every other plane of the application
    uint64_t value = 0;              // the value to set (== the current one: nothing to write)
};

/** The zpos that puts our plane (`ourId`) ABOVE the application's primary plane and BELOW every other plane the application has on the CRTC (its pointer / sprite planes), by the kernel's order.
    `rangeMin` .. `rangeMax`: what the property allows; `mutableProp` false (immutable or no property): not possible. The value is the LOWEST that satisfies the rule (directly above the primary plane), also
    when the plane's `current` value would do: the application's pointer plane may be hidden now (not on the CRTC, so not in `others`) and come later at a low zpos. Not possible (the range does not allow it, no primary plane known, ...): the caller keeps today's behaviour (the plane as discovery found it, above every application plane). */
inline ZposChoice chooseZpos (uint32_t ourId, bool mutableProp, uint64_t rangeMin, uint64_t rangeMax, uint64_t current, const StackPlane* primary, const std::vector<StackPlane>& others)
{
    ZposChoice c;
    if (! mutableProp || primary == nullptr || primary->id == 0 || rangeMin > rangeMax)
        return c;
    auto ok = [&] (uint64_t z)
    {
        if (z < rangeMin || z > rangeMax || ! stackedAbove (z, ourId, primary->zpos, primary->id))
            return false;
        for (const StackPlane& o : others)
            if (o.id != ourId && o.id != primary->id && ! stackedAbove (o.zpos, o.id, z, ourId))
                return false;
        return true;
    };
    (void) current;
    // the lowest value above the primary: its zpos when our id is the higher one (equal zpos: the higher id is above), else one more
    uint64_t z = std::max<uint64_t> (rangeMin, ourId > primary->id ? primary->zpos : primary->zpos + 1);
    if (ok (z))
    {
        c.possible = true;
        c.value = z;
    }
    return c;
}

enum class PointerStacking { None, Above, Covered, Unknown };

/** Where the application's other planes (its pointer) lie against ours: Above (every one is above ours: visible), Covered (one of them lies under ours: our picture hides it), None (it has no other
    plane), Unknown (zpos is not known for some plane: the hardware's own order, as on the Key 61). */
inline PointerStacking pointerStacking (uint32_t ourId, bool ourHasZpos, uint64_t ourZpos, const std::vector<PlaneInfo>& others)
{
    PointerStacking r = PointerStacking::None;
    for (const PlaneInfo& o : others)
    {
        if (o.id == ourId)
            continue;
        if (! o.hasZpos || ! ourHasZpos)
            return PointerStacking::Unknown;
        if (! stackedAbove (o.zpos, o.id, ourZpos, ourId))
            return PointerStacking::Covered;
        r = PointerStacking::Above;
    }
    return r;
}
inline const char* pointerStackingText (PointerStacking p) noexcept
{
    return p == PointerStacking::Above ? "above ours" : p == PointerStacking::Covered ? "covered by ours" : p == PointerStacking::None ? "none" : "unknown (no zpos on every plane)";
}

/** After a plane failed or was taken by another program: the next spare. false: none left. */
inline bool advancePlane (DisplayInfo& d)
{
    if (d.current + 1 >= d.candidates.size())
        return false;
    ++d.current;
    return true;
}

/** A list of CPUs as the kernel writes it ("0-3,6") to a sorted vector of numbers. */
inline std::vector<int> parseCpuList (const std::string& text)
{
    std::vector<int> out;
    const char* p = text.c_str();
    while (*p && *p != '\n')
    {
        char* end = nullptr;
        const long a = std::strtol (p, &end, 10);
        if (end == p)
            break;
        long b = a;
        if (*end == '-')
        {
            p = end + 1;
            b = std::strtol (p, &end, 10);
        }
        for (long c = a; c <= b && c < 1024; ++c)
            out.push_back (static_cast<int> (c));
        if (*end == ',')
            p = end + 1;
        else
            break;
    }
    std::sort (out.begin(), out.end());
    out.erase (std::unique (out.begin(), out.end()), out.end());
    return out;
}

/** "0-3,6" from a sorted vector. */
inline std::string formatCpuList (const std::vector<int>& cpus)
{
    std::string s;
    for (size_t i = 0; i < cpus.size();)
    {
        size_t j = i;
        while (j + 1 < cpus.size() && cpus[j + 1] == cpus[j] + 1)
            ++j;
        if (! s.empty())
            s += ",";
        s += std::to_string (cpus[i]);
        if (j > i)
            s += "-" + std::to_string (cpus[j]);
        i = j + 1;
    }
    return s.empty() ? "none" : s;
}

struct ThreadInfo
{
    std::string name;
    std::vector<int> cpus;           // the affinity of the thread
};

struct CpuPlan
{
    std::vector<int> allowed;        // where the overlay thread and the helper run
    std::vector<int> avoided;        // cores taken out: isolated for the audio threads, or the audio threads' own
    std::vector<std::string> audioThreads;
    bool changed = false;            // allowed differs from `online` (otherwise nothing is set)
};

inline bool nameIsAudio (const std::string& name)
{
    std::string n;
    for (char c : name)
        n += static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
    return n.find ("audio") != std::string::npos;
}

/** The cores for the overlay. The cores the kernel isolates for the audio threads (/sys/devices/system/cpu/isolated) are never used. From the rest the cores the audio threads of the
    process are pinned to (threads named like AudioWorker / Audio Processing whose affinity is narrower than the online set) are taken out, as long as something is left: when the audio
    threads sit on every core that is not isolated (the Key 61 pins AudioWorker0 to AudioWorker3 to its four cores, two of them isolated), they are not taken out and the overlay shares
    the non isolated cores with them. When even that leaves nothing, all online cores stay allowed (the plan changes nothing). */
inline CpuPlan planCpus (const std::vector<int>& online, const std::vector<int>& isolated, const std::vector<ThreadInfo>& threads)
{
    CpuPlan plan;
    std::vector<int> audio;
    for (const ThreadInfo& t : threads)
    {
        if (! nameIsAudio (t.name) || t.cpus.empty() || t.cpus.size() >= online.size())
            continue;
        plan.audioThreads.push_back (t.name);
        for (int c : t.cpus)
            audio.push_back (c);
    }
    std::sort (audio.begin(), audio.end());
    audio.erase (std::unique (audio.begin(), audio.end()), audio.end());
    std::vector<int> base;                                   // online and not isolated
    for (int c : online)
        if (! std::binary_search (isolated.begin(), isolated.end(), c))
            base.push_back (c);
    if (base.empty())
        base = online;
    for (int c : base)
        if (! std::binary_search (audio.begin(), audio.end(), c))
            plan.allowed.push_back (c);
    if (plan.allowed.empty())
        plan.allowed = base;                                 // the audio threads are everywhere: share the cores that are not isolated
    for (int c : online)
        if (! std::binary_search (plan.allowed.begin(), plan.allowed.end(), c))
            plan.avoided.push_back (c);
    plan.changed = plan.allowed != online;
    return plan;
}

}} // namespace synthyboy_mpc::overlay
