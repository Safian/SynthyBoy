// SynthyBoy - MPC overlay: the factory of the page views (page_view.h): which element a view kind of the layout (overlay_layout.json) shows, and the readout targets it needs.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <memory>
#include <vector>

#include "env_ui.h"
#include "filter_ui.h"
#include "fx_ui.h"
#include "lfo_ui.h"
#include "macro_ui.h"
#include "master_ui.h"
#include "mixer_ui.h"
#include "mod_ui.h"
#include "osc_page.h"
#include "overlay_layout.h"
#include "page_view.h"
#include "voice_ui.h"

namespace synthyboy_mpc { namespace overlay { namespace pages {

using FilterPage = PageView<FilterUi>;
using MixerPage = PageView<MixerUi>;
using FxPage = PageView<FxUi>;
using ModPage = PageView<ModUi>;
using MacrosPage = PageView<MacroUi>;
using MasterPage = PageView<MasterUi>;
using VoicePage = PageView<VoiceUi>;
using EnvPage = PageView<EnvUi>;
using LfoPage = PageView<LfoUi>;
using OscPage = PageView<OscPageUi>;

/** The table indices a filter page reads live: the cutoff and the resonance of filter 1, then of filter 2 (synthyboy::FilterResponse::readoutIndices). */
inline std::vector<int> filterTargets()
{
    std::vector<int> t;
    for (int f = 1; f <= 2; ++f)
    {
        int idx[2];
        synthyboy::FilterResponse::readoutIndices (f, idx);
        t.push_back (idx[0]);
        t.push_back (idx[1]);
    }
    return t;
}

/** The view of a layout entry whose kind is a page the plugin draws entirely (not the Play view). Null for any other kind. */
inline std::unique_ptr<IView> make (const layout::View& v)
{
    switch (v.kind)
    {
        case layout::kKindFilterUi: return std::make_unique<FilterPage> (v.name, v.index, filterTargets());
        case layout::kKindMixer: return std::make_unique<MixerPage> (v.name, v.index);
        case layout::kKindFx: return std::make_unique<FxPage> (v.name, v.index);
        case layout::kKindMod: return std::make_unique<ModPage> (v.name, v.index);
        case layout::kKindMacros: return std::make_unique<MacrosPage> (v.name, v.index);
        case layout::kKindMaster: return std::make_unique<MasterPage> (v.name, v.index);
        case layout::kKindVoice: return std::make_unique<VoicePage> (v.name, v.index);
        case layout::kKindEnvUi: return std::make_unique<EnvPage> (v.name, v.index);
        case layout::kKindLfoUi: return std::make_unique<LfoPage> (v.name, v.index);
        case layout::kKindOscUi: return std::make_unique<OscPage> (v.name, v.index, OscPageUi::targetsOf (v.index));
        default: return nullptr;
    }
}

}}} // namespace synthyboy_mpc::overlay::pages
