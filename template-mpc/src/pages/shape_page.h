// MpcKit Demo - PAGE 2 "Shape": a DISPLAY component fed by the plugin (the envelope graph, ui::EnvGraph) and four knobs in two Q groups.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// WHAT THIS PAGE ADDS to main_page.h: a component that is NOT one parameter. The graph shows the attack and the release, so the page
//   - feeds it every frame in pullExtra () (the plugin's values -> the graph's state: a plain struct, the graph owns no truth),
//   - registers it with addPart () (it is drawn and touched like the others; its changes are not bound to a parameter),
//   - handles its changes in onChange () (the graph says WHICH handle moved and the new normalised time; the page writes the parameter).
// TO SHOW ANOTHER PICTURE: a different display component (ui::FilterDisplay-like pictures live in shell-mpc/ui/, docs/ui-components/ lists them) is fed and handled the same way: state in,
// Change out. TO ADD A HANDLE: map one more envgraph handle to a parameter in onChange () and fill its state in pullExtra ().
// The time axis of ui::EnvGraph (envgraph::timeFromNorm) is 0 .. 10 000 ms with skew 0.3: the attack and release parameters of demo_params.h were declared with exactly that range, so the
// normalised parameter value IS the graph's normalised time and no conversion is needed.
#pragma once

#include "../../../shell-mpc/kit/kit.h"
#include "../../../shell-mpc/ui/env_graph.h"
#include "../log_compat.h"

namespace demo {

namespace ui = synthyboy_mpc::ui;

class ShapePage : public mpckit::PageBase
{
public:
    static constexpr int kElementPage = 2;
    using Type = mpckit::Binder::Type;
    enum Id : int { kIdAttack = 1, kIdRelease, kIdCutoff, kIdResonance, kIdGraph = 10 };

    ShapePage() : attack_ ("Attack"), release_ ("Release"), cutoff_ ("Cutoff"), resonance_ ("Reson.")
    {
        bind (kIdAttack, attack_, Type::Knob, "attack");
        bind (kIdRelease, release_, Type::Knob, "release");
        bind (kIdCutoff, cutoff_, Type::Knob, "cutoff");
        bind (kIdResonance, resonance_, Type::Knob, "resonance");
        addPart (kIdGraph, graph_);
        graph_.state.shape.t[0] = graph_.state.shape.t[2] = graph_.state.shape.t[3] = 0.0;   // no delay, hold or decay: an attack - release envelope
        graph_.state.shape.sustain = 1.0;
        graph_.state.defaults = graph_.state.shape;
        graph_.state.locked = true;

        group ({ &attack_, &release_ });                    // Q-Link 1, 2
        group ({ &cutoff_, &resonance_ });                  // the next group: Q-Link 5, 6 (a touch on a knob makes its group the active one)
        MPCKIT_LOG ("page", DEMO_DEBUG, "shape page built");
        relayout();
    }

protected:
    void layout (const ui::Rect& tab) override
    {
        graph_.place ({ 40, tab.y + 20, 1200, 360 });
        const int row = tab.y + 410;                        // 16 px of Q lane above the knobs
        attack_.place ({ 40, row, 270, 200 });
        release_.place ({ 330, row, 270, 200 });
        cutoff_.place ({ 640, row, 270, 200 });
        resonance_.place ({ 930, row, 270, 200 });
    }

    /** Feeds the graph from the parameters. */
    void pullExtra (mpckit::IInstance& inst, double /*nowMs*/, const mpckit::PageInput& /*in*/) override
    {
        const mpckit::IParamSource& p = inst.params();
        const mpckit::Binder::Item* a = binder().item (kIdAttack);
        const mpckit::Binder::Item* r = binder().item (kIdRelease);
        if (a == nullptr || r == nullptr || a->param < 0 || r->param < 0)
            return;
        ui::EnvGraph::State& s = graph_.state;
        s.shape.t[1] = p.normOf (a->param, inst.param (a->param));
        s.shape.t[4] = p.normOf (r->param, inst.param (r->param));
        s.shape.timeMs[1] = inst.param (a->param);
        s.shape.timeMs[4] = inst.param (r->param);
        s.defaults.t[1] = p.normOf (a->param, p.desc (a->param).def);
        s.defaults.t[4] = p.normOf (r->param, p.desc (r->param).def);
        p.valueText (a->param, inst.param (a->param), s.text[ui::envgraph::Attack], sizeof s.text[0]);
        p.valueText (r->param, inst.param (r->param), s.text[ui::envgraph::Release], sizeof s.text[0]);
    }

    /** A handle of the graph moved. */
    void onChange (const ui::Change& ch, mpckit::IInstance& inst, mpckit::UiStack&) override
    {
        if (ch.id != kIdGraph)
            return;
        const ui::EnvGraph::Edit& e = graph_.edit();
        const int which = e.handle == ui::envgraph::Attack ? kIdAttack : (e.handle == ui::envgraph::Release ? kIdRelease : 0);
        if (which == 0)
            return;                                         // the other handles have no parameter in this plugin: the next frame draws them back
        if (const mpckit::Binder::Item* it = binder().item (which); it != nullptr && it->param >= 0)
        {
            MPCKIT_LOG ("param", DEMO_DEBUG, "graph handle %d", e.handle);
            inst.writeParam (it->param, static_cast<float> (e.t));   // the normalised time IS the normalised parameter (see the top of the file)
        }
    }

private:
    ui::Knob attack_, release_, cutoff_, resonance_;
    ui::EnvGraph graph_;
};

} // namespace demo
