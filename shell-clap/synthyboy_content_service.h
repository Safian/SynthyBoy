// SynthyBoy - the CLAP extension "synthyboy.content" on top of a ContentHost (REMAINDER item 3).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The implementation of synthyboy_plugin_content_t (shell-clap/synthyboy_content_ext.h) is shared by the native shell (shell-clap/synthyboy_clap.cpp) and the JUCE-wrapped CLAP
// build (shell-juce/ClapBridge.cpp): each gives the functions the plug-in behind a clap_plugin as a ContentHost. All calls are on the main thread.
#pragma once

#include <clap/clap.h>

#include "synthyboy/engine.h"
#include "synthyboy/offline_render.h"
#include "synthyboy/params.h"
#include "synthyboy/preferences.h"
#include "synthyboy/state.h"
#include "synthyboy/user_content.h"
#include "synthyboy_content_ext.h"

namespace synthyboy
{
/** What the extension needs from a plug-in. The values and the content are working copies: begin () refreshes them at the start of every extension function, the setters
    and the *Changed / *Refresh calls hand the result to the engine and the host. */
class ContentHost
{
public:
    virtual ~ContentHost() = default;

    /** Main thread, first call of every extension function: values () and content () hold the current state of the plug-in. */
    virtual void begin() = 0;
    virtual UserContentStore& store() = 0;
    virtual EngineCapsMask engineCaps() = 0;   // the capabilities of the engine (kCapNone without one)
    virtual ParamValues& values() = 0;
    virtual StateContent& content() = 0;
    virtual const Preferences& prefs() = 0;

    /** One parameter changed (the working copy, the engine and the host know it). `constrained` is in the range of the parameter. */
    virtual void setValue (const ParamInfo& info, float constrained) = 0;
    /** values () was replaced as a whole (a sample root, a Vital preset): the engine and the host get every value. */
    virtual void applyAllValues() = 0;
    /** content () changed: the engine gets it and the host is told that the state is dirty. */
    virtual void contentChanged() = 0;
    /** content () has the same references but a file behind one is gone: the engine reads it again (no dirty flag). */
    virtual void contentRefresh() = 0;
    /** The host should read the parameter values again. */
    virtual void valuesRescan() = 0;

    /** An offline render of the plug-in (the engine of the high tier on a private copy, offline_render.h). */
    virtual bool renderOffline (const OfflineRequest& request, OfflineAudio& audio) = 0;

    struct LastNote
    {
        bool valid = false;
        int key = 60;
        float velocity = 0.8f;
        double heldSeconds = 1.0;
    };
    virtual LastNote lastNote() = 0;
    virtual double tempoBpm() = 0;
};

/** Finds the ContentHost of a clap_plugin of this module (null for a plug-in that is not ours). Set once, before the first extension call. */
using ContentHostResolver = ContentHost* (*) (const clap_plugin_t*);
void setContentHostResolver (ContentHostResolver resolver) noexcept;

/** The vtable the plug-in returns for SYNTHYBOY_EXT_CONTENT. */
const synthyboy_plugin_content_t* contentExtension() noexcept;
} // namespace synthyboy
