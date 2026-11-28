// SynthyBoy - MPC overlay: the PAGE view of SynthyBoy: page_view_base.h (the generic view of a page the plugin draws entirely: BasicPageView, NoHooks, the ground and the scene hash) with SynthyBoy's hooks - the macro
// names of the instance on screen (macro_names.h) active while a page updates, draws and handles input, the loading bar of IInstanceView::tablesLoading () and the live values of the engine for the page
// (page::Input). PageView<Ui> is BasicPageView<Ui, MacroNameHooks>.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "instance_view.h"
#include "macro_names.h"
#include "page_view_base.h"
#include "synthyboy/readout.h"
#include "view_cast.h"

namespace synthyboy_mpc { namespace overlay {

namespace page
{
/** What the controller hands an element besides the instance: the readout targets of this view (IView::targets order) and the instance number the tab names (Env 3, LFO 2, Filter 2: 3, 2, 2; 1 for the others) are
    the kit's PageInput; SynthyBoy's pages get the live values of the engine as well. */
struct Input : mpckit::PageInput
{
    const synthyboy::LiveValues* live = nullptr;
};
} // namespace page

/** The hooks of the page view for the pages of SynthyBoy: the macro names of the instance on screen are loaded from its content at every update and made the active ones (NamesScope) for everything the view does; the
    loading bar follows IInstanceView::tablesLoading (); `Instance` is the type the pages take (every instance of SynthyBoy is an IInstanceView), `Input` the input with the live values. A page of a plugin made with the kit
    uses NoHooks (page_view_base.h) instead. */
class MacroNameHooks
{
public:
    using Instance = IInstanceView;
    using Input = page::Input;
    /** Loads the names from the content of the instance. */
    void update (mpckit::IInstance& inst)
    {
        const auto content = static_cast<IInstanceView&> (inst).content();   // the names of the macros the pages show: the ones of this instance, for everything the view does
        names_.load (content.get());
    }
    /** The scope that makes the names the active ones (keep the object for as long as the names are used). */
    NamesScope scope() const noexcept { return NamesScope (names_); }
    /** A wavetable of the program is still being built (the bar at the top of the tab). */
    bool loading (const mpckit::IInstance& inst) const noexcept { return static_cast<const IInstanceView&> (inst).tablesLoading(); }
    /** The live values of the update. */
    void fill (Input& pi, const ViewInput& in) const noexcept { pi.live = liveOf (in); }

private:
    MacroNames names_;
};

/** The page view of an element of SynthyBoy. */
template <class Ui>
using PageView = BasicPageView<Ui, MacroNameHooks>;

}} // namespace synthyboy_mpc::overlay
