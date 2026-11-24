// SynthyBoy - native JUCE-free CLAP entry and wrapper (docs/ARCHITECTURE.md 4).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <clap/clap.h>

#if defined (_WIN32)
 #if defined (SYNTHYBOY_CLAP_EXPORT)
  #define SYNTHYBOY_CLAP_API __declspec(dllexport)
 #else
  #define SYNTHYBOY_CLAP_API
 #endif
#else
 #define SYNTHYBOY_CLAP_API __attribute__ ((visibility ("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Returns the CLAP plugin entry point for statically linked hosts (e.g. iOS / Android / Kick MPC),
    as declared in plugin.json under static.entry. */
SYNTHYBOY_CLAP_API const clap_plugin_entry_t* synthyboy_clap_get_entry (void);

#ifdef __cplusplus
}
#endif
