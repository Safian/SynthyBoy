// SynthyBoy - MPC overlay: the factory of the views: which view (and which element behind a page view, page_view.h) a view kind of the layout (overlay_layout.json) shows, and the readout targets they need.
// SynthyBoyPageFactory is SynthyBoy's mpckit::IPageFactory (kit/page_set.h): the controller asks it for the view behind every PageDef of the page set (migration step 10 of docs/ui-components/TEMPLATE.md).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <memory>
#include <vector>

#ifdef SYNTHYBOY_BACKGROUNDS_MANIFEST_H   // the tests build the other state of the manifest ([macros]) with -DSYNTHYBOY_BACKGROUNDS_MANIFEST_H='"file.h"'
#include SYNTHYBOY_BACKGROUNDS_MANIFEST_H
#else
#include "backgrounds_manifest.h"
#endif
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

/** The first XL host function key contains Play and Macros. Its active bar runs
    from x=17 to 195 at y=740. Use the button interior, below the 628 px tab. */
inline void configureNativeTab (IView& view, const char* name)
{
    if (const BackgroundTab* bg = backgroundOf (name))
    {
        view.setSentinel ({ bg->sentinelX, page::kTabY + bg->sentinelY, 5, 5 }, bg->sentinelRgb);
        // Only the two native function-key pages participate in the XL page
        // handoff; other pages may use a static sentinel background but must
        // not inherit the Play/Macros touch key.
        if ((std::strcmp (name, "play1") == 0 || std::strcmp (name, "macros") == 0)
            && hasHostBackground ("play1") && hasHostBackground ("macros"))
            view.setNativeTabKey ({ 17, 741, 179, 58 });
    }
}

/** The view of a page the plugin draws entirely (not the Play view): by the kind and the instance number of its layout entry. Null for any other kind. */
inline std::unique_ptr<IView> make (const char* name, int kind, int index)
{
    std::unique_ptr<IView> view;
    switch (kind)
    {
        case layout::kKindFilterUi:
        {
            auto filter = std::make_unique<FilterPage> (name, index, filterTargets());
            view = std::move (filter);
            break;
        }
        case layout::kKindMixer: view = std::make_unique<MixerPage> (name, index, filterTargets()); break;
        case layout::kKindFx: view = std::make_unique<FxPage> (name, index); break;
        case layout::kKindMod: view = std::make_unique<ModPage> (name, index); break;
        case layout::kKindMacros: view = std::make_unique<MacrosPage> (name, index); break;
        case layout::kKindMaster: view = std::make_unique<MasterPage> (name, index); break;
        case layout::kKindVoice: view = std::make_unique<VoicePage> (name, index); break;
        case layout::kKindEnvUi: view = std::make_unique<EnvPage> (name, index); break;
        case layout::kKindLfoUi: view = std::make_unique<LfoPage> (name, index); break;
        case layout::kKindOscUi: view = std::make_unique<OscPage> (name, index, OscPageUi::readoutTargets()); break;
        default: return nullptr;
    }
    configureNativeTab (*view, name);
    return view;
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
    /** `renderer`: the curve renderer of the views of the first overlay (null: the factory has a CPU renderer of its own); `fixedCostMs`: their fixed compute cost (-1: measured). The renderer given must outlive the factory. */
    explicit SynthyBoyPageFactory (ICurveRenderer* renderer = nullptr, double fixedCostMs = -1.0) noexcept : renderer_ (renderer != nullptr ? *renderer : own_), fixedCost_ (fixedCostMs) {}

    std::unique_ptr<IView> make (const mpckit::PageDef& d) const override
    {
        switch (d.kind)
        {
            case layout::kKindFilter: return std::make_unique<FilterView> (renderer_, fixedCost_);
            case layout::kKindLfo: return std::make_unique<LfoView> (d.index, fixedCost_);
            case layout::kKindEnv: return std::make_unique<EnvView> (d.index, pages::layoutViewOf (d));
            case layout::kKindSpectrum: return std::make_unique<SpectrumView>();
            case layout::kKindPlay:
            {
                auto p = std::make_unique<PlayView> (d.index, fixedCost_);
                pages::configureNativeTab (*p, d.name);
                return p;
            }
            default:
                if (layout::isPageKind (d.kind))
                    return pages::make (d.name, d.kind, d.index);
                return std::make_unique<NullView> (d.name);
        }
    }

    /** The readout targets of all pages of SynthyBoy without a controller or a renderer (the shell asks before the overlay exists). */
    static void targetsOfAllPages (std::vector<int>& out)
    {
        FilterRendererStub stub;
        SynthyBoyPageFactory (&stub, -1.0).readoutTargets (out);
    }

    /** The live block of the engine: synthyboy::LiveValues (IInstanceView::readLive). */
    size_t liveBytes() const noexcept override { return sizeof (synthyboy::LiveValues); }
    /** The readout values of a view whose targets start at `base` in the engine's list and number `count`: the pointer into the block only when ALL of them are in what the engine delivered (numTargets) and inside the
        array; otherwise null, and the view draws without live values. */
    const float* viewTargets (const void* live, int base, int count) const noexcept override
    {
        const synthyboy::LiveValues& lv = *static_cast<const synthyboy::LiveValues*> (live);
        if (base < 0 || count <= 0 || count > synthyboy::LiveValues::kMaxTargets - base)
            return nullptr;
        return lv.numTargets >= base + count ? lv.target + base : nullptr;
    }

    void readoutTargets (std::vector<int>& out) const override
    {
        out.clear();
        FilterRendererStub stub;
        std::vector<int> range;
        for (int i = 0; i < layout::kNumViews; ++i)
        {
            range.clear();
            const layout::View& v = layout::kViews[i];
            if (v.kind == layout::kKindFilter)
                FilterView (stub, -1.0).targets (range);
            else if (v.kind == layout::kKindPlay)
                PlayView (v.index, -1.0).targets (range);
            else if (layout::isPageKind (v.kind))
                pages::make (v)->targets (range);
            mpckit::appendReadoutRange (out, range);
        }
    }

private:
    CpuCurveRenderer own_;
    ICurveRenderer& renderer_;
    double fixedCost_;
};

}} // namespace synthyboy_mpc::overlay
