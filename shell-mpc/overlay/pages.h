// SynthyBoy - MPC overlay: the factory of the views: which view (and which element behind a page view, page_view.h) a view kind of the layout (overlay_layout.json) shows, and the readout targets they need.
// SynthyBoyPageFactory is SynthyBoy's mpckit::IPageFactory (kit/page_set.h): the controller asks it for the view behind every PageDef of the page set (migration step 10 of docs/ui-components/TEMPLATE.md).
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
#include "../kit/page_set.h"
#include "osc_page.h"
#include "overlay_layout.h"
#include "page_view.h"
#include "play_view.h"
#include "views.h"
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

/** The view of a page the plugin draws entirely (not the Play view): by the kind and the instance number of its layout entry. Null for any other kind. */
inline std::unique_ptr<IView> make (const char* name, int kind, int index)
{
    switch (kind)
    {
        case layout::kKindFilterUi: return std::make_unique<FilterPage> (name, index, filterTargets());
        case layout::kKindMixer: return std::make_unique<MixerPage> (name, index);
        case layout::kKindFx: return std::make_unique<FxPage> (name, index);
        case layout::kKindMod: return std::make_unique<ModPage> (name, index);
        case layout::kKindMacros: return std::make_unique<MacrosPage> (name, index);
        case layout::kKindMaster: return std::make_unique<MasterPage> (name, index);
        case layout::kKindVoice: return std::make_unique<VoicePage> (name, index);
        case layout::kKindEnvUi: return std::make_unique<EnvPage> (name, index);
        case layout::kKindLfoUi: return std::make_unique<LfoPage> (name, index);
        case layout::kKindOscUi: return std::make_unique<OscPage> (name, index, OscPageUi::targetsOf (index));
        default: return nullptr;
    }
}
/** The same for an entry of the generated table (the pictures of the gallery test). */
inline std::unique_ptr<IView> make (const layout::View& v) { return make (v.name, v.kind, v.index); }

/** The entry of the generated table that a PageDef was made from (the views of the first overlay take their own region and markers). */
inline layout::View layoutViewOf (const mpckit::PageDef& d) noexcept
{
    layout::View v {};
    v.name = d.name; v.kind = d.kind; v.index = d.index; v.opaque = d.opaque;
    v.region = { d.region.x, d.region.y, d.region.w, d.region.h };
    v.instance = { d.instance.x, d.instance.y, d.instance.w, d.instance.h };
    for (int m = 0; m < 2; ++m)
        v.markers[m] = { { d.markers[m].rect.x, d.markers[m].rect.y, d.markers[m].rect.w, d.markers[m].rect.h }, d.markers[m].rgb, d.markers[m].cover };
    return v;
}
} // namespace pages

/** SynthyBoy's view factory (mpckit::IPageFactory): `make (PageDef)` is the switch over the view kinds of overlay_layout.json - the views of the first overlay (the filter curve, the LFO shape, the envelope marker, the
    spectrum), the Play view and the pages of the elements (pages::make) - and `readoutTargets` the table indices all of them want in the engine's live readout (what Controller::readoutTargets was). `renderer` and
    `fixedCostMs` are what the filter curve views need (the controller's renderer and its fixed compute cost, -1: measured). */
class SynthyBoyPageFactory final : public mpckit::IPageFactory
{
public:
    SynthyBoyPageFactory (ICurveRenderer& renderer, double fixedCostMs) noexcept : renderer_ (renderer), fixedCost_ (fixedCostMs) {}

    std::unique_ptr<IView> make (const mpckit::PageDef& d) const override
    {
        switch (d.kind)
        {
            case layout::kKindFilter: return std::make_unique<FilterView> (d.index, renderer_, fixedCost_);
            case layout::kKindLfo: return std::make_unique<LfoView> (d.index, fixedCost_);
            case layout::kKindEnv: return std::make_unique<EnvView> (d.index, pages::layoutViewOf (d));
            case layout::kKindSpectrum: return std::make_unique<SpectrumView>();
            case layout::kKindPlay: return std::make_unique<PlayView> (d.index, fixedCost_);
            default:
                if (layout::isPageKind (d.kind))
                    return pages::make (d.name, d.kind, d.index);
                return std::make_unique<NullView> (d.name);
        }
    }

    void readoutTargets (std::vector<int>& out) const override
    {
        out.clear();
        FilterRendererStub stub;
        for (int i = 0; i < layout::kNumViews; ++i)
        {
            const layout::View& v = layout::kViews[i];
            if (v.kind == layout::kKindFilter)
                FilterView (v.index, stub, -1.0).targets (out);
            else if (v.kind == layout::kKindPlay)
                PlayView (v.index, -1.0).targets (out);
            else if (layout::isPageKind (v.kind))
                pages::make (v)->targets (out);
        }
    }

private:
    ICurveRenderer& renderer_;
    double fixedCost_;
};

}} // namespace synthyboy_mpc::overlay
