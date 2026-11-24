// MpcKit - BRANDING: the names the overlay runtime gives to the files, the helper process and the environment variable it uses, so that two plugins made with the kit do not share them. Standard library only.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The runtime keeps a few small files in /tmp (the kill switch, the rotation override, the status file, the GPU clock state, the rest value of the plane's zpos, the helper's log ...) and looks for its GPU
// helper executable next to the plugin binary. Their names carry the plugin's prefix: /tmp/<prefix>-overlay-off, <folder of the plugin>/<helperName>-<arch>. The DEFAULT is SynthyBoy's ("synthyboy"),
// so nothing changes for it; a plugin made with the kit passes its own to mpckit::Overlay::attach (or calls setBranding before) and the whole runtime follows.
//
// THREADS. The branding is set once, before the first instance is attached; it is read by every thread after that and never changes. The object given to setBranding must outlive the overlay (a constant
// of the plugin).
#pragma once

#include <string>

namespace mpckit {

struct Branding
{
    const char* prefix = "synthyboy";                   // the prefix of the files in /tmp: /tmp/<prefix>-overlay-off, -rot, -nogpu, -status.txt, -gpu.log ...
    const char* helperName = "synthyboy-overlay-gpu";   // the GPU helper executable: <folder of the plugin>/<helperName>-arm64 or -arm32 (the package puts it beside the plugin binary)
    const char* qKnobsEnv = "SYNTHYBOY_MPC_Q_KNOBS";    // the environment variable that overrides the number of Q-Link knobs of the device (4 or 16)
};

namespace detail
{
inline const Branding*& brandingSlot() noexcept
{
    static const Branding defaults;
    static const Branding* current = &defaults;
    return current;
}
} // namespace detail

/** The branding in force (SynthyBoy's until setBranding is called). */
inline const Branding& branding() noexcept { return *detail::brandingSlot(); }
/** Sets the branding (before the first instance is attached). */
inline void setBranding (const Branding& b) noexcept { detail::brandingSlot() = &b; }

/** "/tmp/<prefix>-<name>": a file of the runtime in /tmp ("overlay-off", "overlay-status.txt" ...). */
inline std::string tmpFile (const Branding& b, const char* name) { return std::string ("/tmp/") + b.prefix + "-" + name; }
inline std::string tmpFile (const char* name) { return tmpFile (branding(), name); }

} // namespace mpckit
