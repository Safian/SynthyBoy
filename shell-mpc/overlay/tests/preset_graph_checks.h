// SynthyBoy - MPC preset recall through the production page and scene paths.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "demo_common.h"
#include "synthyboy/content_store.h"
#include "synthyboy/engine.h"
#include "synthyboy/preset.h"
#include "synthyboy/user_content.h"
#include "synthyboy/wave_convert.h"

namespace preset_graph
{
using namespace synthyboy_mpc;
using namespace synthyboy_mpc::overlay;
using ovt::check;

struct Instance : demo::Inst
{
    int peek = -1, filterAction = -1, storedCurveWrites = 0;
    nativeui::fxheader::Bank fxHeader;
    void setNativeFxHeader (const nativeui::fxheader::State& state) noexcept override
    {
        fxHeader.publish (state, generation);
    }
    bool takeNativeFxHeader (int field, const nativeui::fxheader::State& expected, float& value) noexcept override
    {
        return fxHeader.take (field, expected, value);
    }
    void setLfoShape (int, const synthyboy::ContentLfoShape&) override { ++storedCurveWrites; }
    void setFilterCurve (int, const std::vector<synthyboy::ContentFilterCurvePoint>&, uint64_t) override { ++storedCurveWrites; }
    int takeNativeMixerPeek() noexcept override { return std::exchange (peek, -1); }
    bool takeNativeFilterSlot (int slot, const nativeui::FxCell&, float& value) noexcept override
    {
        if (slot != nativeui::FilterAction || filterAction < 0)
            return false;
        value = static_cast<float> (std::exchange (filterAction, -1)) / nativeui::FilterLastAction;
        return true;
    }
    const synthyboy::IContentStore* store = nullptr;
    const synthyboy::IContentStore* contentStore() const noexcept override { return store; }
    OscContentSnapshot oscContent() const override { return { contentV, prepared }; }
    std::shared_ptr<const synthyboy::own::ContentSet> prepared;
};

struct Preset
{
    synthyboy::ParamValues values = synthyboy::ParamValues::defaults();
    synthyboy::StateContent content;

    void set (const char* key, float plain)
    {
        const int index = demo::Inst::idx (key);
        check (index >= 0, std::string ("preset graph fixture parameter: ") + key);
        if (index >= 0)
            values.plain[index] = plain;
    }

    void apply (const std::string& name, Instance& instance) const
    {
        auto meta = synthyboy::PresetMeta::initDefault();
        meta.name = name;
        const auto data = synthyboy::savePreset (meta, values, content);
        synthyboy::ParamValues decoded = synthyboy::ParamValues::defaults();
        auto shapes = std::make_shared<synthyboy::StateContent>();
        synthyboy::PresetMeta loaded;
        const auto result = synthyboy::loadPreset (data.data(), data.size(), loaded, decoded, *shapes);
        check (result.status == synthyboy::LoadStatus::ok && loaded.name == name && *shapes == content,
               "parsed preset graph fixture " + name);
        for (size_t i = 0; i < synthyboy::paramCount(); ++i)
            instance.setAt (static_cast<int> (i), decoded.plain[i]);
        instance.contentV = std::move (shapes);
        if (instance.store != nullptr)
        {
            auto engine = synthyboy::createEngine (synthyboy::EngineKind::own);
            engine->setContentStore (instance.store);
            engine->setContent (*instance.contentV);
            instance.prepared = engine->preparedOscContent();
            check (instance.prepared != nullptr, "the oscillator fixture publishes prepared content");
        }
        ++instance.generation;
    }
};

struct Page
{
    Instance& inst;
    std::unique_ptr<IView> view;
    UiStack stack;
    ViewInput input;

    Page (const char* name, Instance& instance) : inst (instance)
    {
        const auto* entry = std::find_if (std::begin (layout::kViews), std::end (layout::kViews),
                                          [name] (const auto& row) { return std::string (row.name) == name; });
        if (entry == std::end (layout::kViews))
            std::abort();
        if (entry->kind == layout::kKindPlay)
        {
            view = std::make_unique<PlayView> (entry->index, 0.0);
            pages::configureNativeTab (*view, name);
        }
        else
            view = pages::make (*entry);
        if (view->pageElement() >= 0)
            stack.openFull (view->pageElement());
        input.instance = &inst;
        input.io = &inst;
        input.ui = &stack;
        view->setHostBackground (true);
        view->onShown();
        update();
    }

    template <class V> V& as() { return static_cast<V&> (*view); }

    bool update()
    {
        input.nowMs += 100.0;
        return view->update (input);
    }

    void tap (ui::Rect r)
    {
        const int x = r.x + r.w / 2, y = r.y + r.h / 2;
        const bool owned = view->onTouch ({ TouchEvent::Down, x, y }, stack, inst, input.nowMs);
        view->onTouch ({ TouchEvent::Up, x, y }, stack, inst, input.nowMs + 40.0);
        check (owned && ! r.empty(), std::string (view->name()) + " owns the editor opener");
        update();
    }

    void openPeek (int target)
    {
        inst.peek = target;
        update();
        check (stack.top() == MixerUi::kPagePeek, "the native Mixer PEEK mailbox opens the requested panel");
    }

    void openFxMore()
    {
        namespace header = nativeui::fxheader;
        const auto before = inst.fxHeader.state();
        const auto action = inst.fxHeader.at (header::Page);
        check (before.item >= 0 && action.cell().target == before.item && before.on() && ! before.more(),
               std::string (view->name()) + " publishes native MORE for the powered FX editor");
        const int writes = inst.writes;
        inst.fxHeader.report (action);
        inst.fxHeader.post (before.item, header::Page, 1.0f, inst.generation);
        update();
        const auto after = inst.fxHeader.state();
        check (after.item == before.item && after.rack == before.rack && after.more() && inst.writes == writes,
               std::string (view->name()) + " native MORE opens the same FX item and rack without writing a parameter");
    }

    void settle()
    {
        // Oscillator and LFO switch from the uncached to cached drawing pass
        // at the end of their post-change display window.
        input.nowMs += 600.0;
        update();
    }

    std::vector<uint32_t> capture (ui::Rect r, const std::string& label)
    {
        auto scene = std::make_unique<gpu::SceneData>();
        gpu::SceneBuilder sb (*scene);
        const bool ready = view->describe (sb, 0, page::kW, page::kH);
        if (! ready || r.empty() || ! ui::Rect { 0, 0, page::kW, page::kH }.contains (r))
        {
            check (false, label + " has a valid scene and graph rectangle");
            return {};
        }
        std::vector<uint32_t> pixels, crop;
        renderScene (*scene, pixels);
        crop.reserve (static_cast<size_t> (r.w * r.h));
        for (int y = r.y; y < r.bottom(); ++y)
        {
            const auto first = pixels.begin() + y * page::kW + r.x;
            crop.insert (crop.end(), first, first + r.w);
        }
        return crop;
    }
};

template <class Open, class Plot>
void roundTrip (const char* label, const char* tab, const Preset& a, const Preset& b, Open open, Plot plot,
                int oscSelection = 1, int lfoSelection = 1, int filterView = 0,
                const synthyboy::IContentStore* store = nullptr)
{
    const std::string title = label;
    auto load = [&] (Instance& instance, int step)
    {
        instance.playSel[0] = oscSelection;
        instance.playSel[2] = std::min (lfoSelection, LfoUi::kLfos);
        instance.tabViewV[IInstanceView::kTabViewRnd] = std::max (lfoSelection - LfoUi::kLfos, 0);
        instance.tabViewV[IInstanceView::kTabViewFilter] = filterView;
        instance.store = store;
        (step == 1 ? b : a).apply (title + (step == 1 ? " B" : " A"), instance);
    };
    Instance inst;
    load (inst, 0);
    Page held (tab, inst);
    open (held);
    std::vector<uint32_t> pixelsA, pixelsB;
    for (int step = 0; step < 3; ++step)
    {
        if (step != 0)
            load (inst, step);
        const bool changed = held.update();
        held.settle();
        const auto live = held.capture (plot (held), title);
        const int depth = held.stack.depth();
        Instance referenceInstance;
        load (referenceInstance, step);
        Page reopened (tab, referenceInstance);
        open (reopened);
        reopened.settle();
        const auto reference = reopened.capture (plot (reopened), title);
        check (! live.empty() && live == reference && depth == reopened.stack.depth(),
               title + " open " + (step == 0 ? "A" : step == 1 ? "B" : "A2") + " matches a reopened editor");
        if (step != 0)
            check (changed, title + " publishes the preset change without a live input");
        bool quietly = true;
        for (int i = 0; i < 4; ++i)
            quietly = ! held.update() && quietly;
        check (quietly && held.capture (plot (held), title) == live, title + " remains still once the preset is visible");
        if (step == 0)
            pixelsA = live;
        else if (step == 1)
            pixelsB = live;
        else
            check (live == pixelsA && pixelsA != pixelsB, title + " has distinct A/B plots and restores the original A pixels");
    }
    check (inst.writes == 0 && inst.scalarWrites == 0 && inst.storedCurveWrites == 0,
           title + " recalls the graph without writing synthesis parameters or curves");
}

inline void openOscDetail (Page& page, OscUi& osc, const char* token)
{
    if (std::string (token) == "unison-gear")
    {
        const auto* box = osc.full().panel.unison();
        page.tap (box ? box->gear.rect() : ui::Rect {});
        return;
    }

    page.tap (osc.full().more.rect());
    auto& detail = osc.specPage();
    const auto* spec = detail.spec();
    for (int i = 0; spec != nullptr && i < spec->numCtls; ++i)
        if (spec->ctls[i].kind == oscspec::K::Page && std::string (spec->ctls[i].key) == token && detail.visible (i))
        {
            if (const auto* control = detail.control (i))
                page.tap (control->rect());
            return;
        }
    check (false, std::string ("Oscillator MORE has a visible ") + token + " opener");
}

inline void runFilter (Preset& a, Preset& b)
{
    auto root = [] (Page&) {};
    auto filter = [] (Page& p) -> FilterUi& { return p.as<pages::FilterPage>().ui(); };
    a = {}; b = {};
    for (int f = 1; f <= 2; ++f)
    {
        const std::string pre = "filter" + std::to_string (f);
        a.set ((pre + ".on").c_str(), 1);
        b.set ((pre + ".on").c_str(), 1);
        a.set ((pre + ".cutoff").c_str(), 175);
        b.set ((pre + ".cutoff").c_str(), 6350);
        const std::string label = "Filter " + std::to_string (f) + " response";
        roundTrip (label.c_str(), "filter1", a, b, root,
                   [&] (Page& p) { return filter (p).full().display.rect().inset (12); }, 1, 1, f - 1);
        roundTrip (("Mixer PEEK " + label).c_str(), "mixer", a, b,
                   [f] (Page& p) { p.openPeek (f - 1); },
                   [] (Page& p) {
                       auto* u = dynamic_cast<ui::FilterFullScreen*> (p.as<pages::MixerPage>().ui().peekRoot());
                       return u ? u->display.rect().inset (12) : ui::Rect {};
                   });
    }
    roundTrip ("Filter F1+F2 result", "filter1", a, b,
               [] (Page& p) {
                   p.inst.filterAction = nativeui::FilterBoth;
                   p.update();
               },
               [&] (Page& p) { return filter (p).isDual() ? filter (p).full().display.rect().inset (12) : ui::Rect {}; },
               1, 1, tabview::encodeFilterSplit (0, 0, tabview::kFilterExpandedFilter));
    b = a;
    a.content.noteCurve = a.content.velocityCurve = {{0, 0}, {0.3f, 0.78f}, {1, 1}};
    b.content.noteCurve = b.content.velocityCurve = {{0, 0}, {0.6f, 0.1f}, {1, 1}};
    for (int v = 2; v <= 3; ++v)
        roundTrip (v == 2 ? "Filter NOTE" : "Filter VELO", "filter1", a, b, root,
                   [&] (Page& p) { return filter (p).curveUi().screen.canvas.plot(); }, 1, 1, v);
    a.set ("filter1.model", FilterUi::kModelPz);
    b.set ("filter1.model", FilterUi::kModelPz);
    a.content.filterCurves = {{"filter1", {{0, -35}, {0.32f, -8}, {1, 20}}}};
    b.content.filterCurves = {{"filter1", {{0, 24}, {0.64f, -35}, {1, -6}}}};
    a.set ("filter2.model", FilterUi::kModelPz);
    b.set ("filter2.model", FilterUi::kModelPz);
    a.content.filterCurves.push_back ({"filter2", {{0, 17}, {0.28f, 26}, {1, -8}}});
    b.content.filterCurves.push_back ({"filter2", {{0, -19}, {0.58f, -11}, {1, 13}}});
    for (int f = 1; f <= 2; ++f)
    {
        const auto label = "Filter " + std::to_string (f) + " PZ SVF EDIT";
        roundTrip (label.c_str(), "filter1", a, b,
                   [] (Page& p) {
                       p.inst.filterAction = nativeui::FilterEditPz;
                       p.update();
                   },
                   [&] (Page& p) { return filter (p).topPage() == FilterUi::kPagePz ? filter (p).pzEditor().canvas.plot() : ui::Rect {}; },
                   1, 1, f - 1);
    }

}

inline void run()
{
    auto root = [] (Page&) {};
    auto envelope = [] (Page& p) -> EnvUi& { return p.as<pages::EnvPage>().ui(); };
    auto lfo = [] (Page& p) -> LfoUi& { return p.as<pages::LfoPage>().ui(); };

    Preset a, b;
    a.set ("env1.attack", 17.0f);
    b.set ("env1.attack", 1450.0f);
    a.set ("env1.sustain", 12.0f);
    b.set ("env1.sustain", 75.0f);
    roundTrip ("ENV standalone", "env1", a, b, root, [&] (Page& p) { return envelope (p).full().graph.plot(); });
    roundTrip ("Play ENV expanded", "play1", a, b,
               [] (Page& p) { p.tap (p.as<PlayView>().envUi()->compact().full.rect()); },
               [] (Page& p) { return p.as<PlayView>().envUi()->full().graph.plot(); });

    a = {}; b = {};
    a.set ("lfo1.shape", 1);
    b.set ("lfo1.shape", 2);
    roundTrip ("LFO standalone built-in", "lfo1", a, b, root, [&] (Page& p) { return lfo (p).full().displayRect().inset (6); });
    roundTrip ("Play LFO expanded built-in", "play1", a, b,
               [] (Page& p) { p.tap (p.as<PlayView>().lfoUi()->compact().full.rect()); },
               [] (Page& p) { return p.as<PlayView>().lfoUi()->full().displayRect().inset (6); });
    a.set ("lfo1.shape", 12);
    b.set ("lfo1.shape", 12);
    synthyboy::ContentLfoShape shape;
    shape.lfo = 1;
    shape.gridX = 8;
    shape.gridY = 8;
    shape.points = {{0.0f, 0.5f}, {0.3f, 0.06f}, {0.7f, 0.85f}, {1.0f, 0.5f}};
    a.content.lfoShapes = { shape };
    shape.points[1].y = 0.92f;
    shape.points[2].y = 0.12f;
    b.content.lfoShapes = { shape };
    roundTrip ("LFO standalone stored shape", "lfo1", a, b, root, [&] (Page& p) { return lfo (p).full().displayRect().inset (6); });
    roundTrip ("Play LFO expanded stored shape", "play1", a, b,
               [] (Page& p) { p.tap (p.as<PlayView>().lfoUi()->compact().full.rect()); },
               [] (Page& p) { return p.as<PlayView>().lfoUi()->full().displayRect().inset (6); });
    roundTrip ("LFO EDIT canvas and grid", "lfo1", a, b,
               [&] (Page& p) { p.tap (lfo (p).full().edit.rect()); },
               [&] (Page& p) { return lfo (p).topPage() == LfoUi::kPageEditor ? lfo (p).editor().canvas.plot() : ui::Rect {}; });
    roundTrip ("Play LFO EDIT", "play1", a, b,
               [] (Page& p) {
                   auto& u = *p.as<PlayView>().lfoUi();
                   p.tap (u.compact().full.rect());
                   p.tap (u.full().edit.rect());
               }, [] (Page& p) {
                   auto& u = *p.as<PlayView>().lfoUi();
                   return u.topPage() == LfoUi::kPageEditor ? u.editor().canvas.plot() : ui::Rect {};
               });

    b = a;
    b.content.lfoShapes[0].gridX = 16;
    b.content.lfoShapes[0].gridY = 4;
    roundTrip ("LFO EDIT grid only", "lfo1", a, b,
               [&] (Page& p) { p.tap (lfo (p).full().edit.rect()); },
               [&] (Page& p) { return lfo (p).topPage() == LfoUi::kPageEditor ? lfo (p).editor().canvas.plot() : ui::Rect {}; });

    a = {}; b = {};
    a.set ("rlfo1.style", 0);
    b.set ("rlfo1.style", 3);
    roundTrip ("RND standalone at rest", "lfo1", a, b, root,
               [&] (Page& p) { return lfo (p).full().displayRect().inset (6); }, 1, 11);
    roundTrip ("Play RND expanded at rest", "play1", a, b,
               [] (Page& p) { p.tap (p.as<PlayView>().lfoUi()->compact().full.rect()); },
               [] (Page& p) { return p.as<PlayView>().lfoUi()->full().displayRect().inset (6); }, 1, 11);

    a = {}; b = {};
    a.set ("osc1.on", 1);
    b.set ("osc1.on", 1);
    a.set ("osc1.wavetable", 0);
    b.set ("osc1.wavetable", 1);
    roundTrip ("Osc A standalone wavetable", "osca", a, b, root,
               [] (Page& p) { return p.as<pages::OscPage>().ui().ui().displayRect().inset (14); });
    roundTrip ("Play Osc A expanded wavetable", "play1", a, b,
               [] (Page& p) { p.tap (p.as<PlayView>().oscUi()->compact().full.rect()); },
               [] (Page& p) { return p.as<PlayView>().oscUi()->displayRect().inset (14); }, 2);
    a.set ("sub.on", 1);
    b.set ("sub.on", 1);
    a.set ("sub.waveform", 0);
    b.set ("sub.waveform", 3);
    roundTrip ("Sub standalone", "oscsub", a, b, root,
               [] (Page& p) { return p.as<pages::OscPage>().ui().ui().displayRect().inset (14); });
    roundTrip ("Play Sub expanded", "play1", a, b,
               [] (Page& p) { p.tap (p.as<PlayView>().oscUi()->compact().full.rect()); },
               [] (Page& p) { return p.as<PlayView>().oscUi()->displayRect().inset (14); });

    auto checkOscDetail = [] (const Preset& first, const Preset& second, const char* name,
                              const char* token, int top, const synthyboy::IContentStore* store = nullptr)
    {
        auto plot = [top] (OscUi& osc)
        {
            if (osc.topPage() != top)
                return ui::Rect {};
            if (top == OscUi::kPageEnvelope)
            {
                const ui::Rect r = osc.detail().displayRect();
                return ui::Rect {r.x + 16, r.y + 38, r.w - 32, r.h - 52};
            }
            return osc.displayRect().inset (14);
        };
        auto osc = [] (Page& p) -> OscUi& { return p.as<pages::OscPage>().ui().ui(); };
        roundTrip (name, "osca", first, second,
                   [&] (Page& p) { openOscDetail (p, osc (p), token); },
                   [&] (Page& p) { return plot (osc (p)); }, 2, 1, 0, store);
        roundTrip ((std::string ("Play ") + name).c_str(), "play1", first, second,
                   [&] (Page& p) {
                       auto& u = *p.as<PlayView>().oscUi();
                       p.tap (u.compact().full.rect());
                       openOscDetail (p, u, token);
                   },
                   [&] (Page& p) { return plot (*p.as<PlayView>().oscUi()); }, 2, 1, 0, store);
    };
    // At this point a/b also contain the Sub variation. The deeper Osc A
    // crops contain only A's graph and never the compact Sub panel.
    checkOscDetail (a, b, "Osc A Frame Map", "map", oscspec::kWtMap);
    checkOscDetail (a, b, "Osc A Unison", "unison-gear", OscUi::kPageUnison);

    {
        Instance inst;
        a.apply ("Osc A detail A", inst);
        Page page ("osca", inst);
        auto& osc = page.as<pages::OscPage>().ui().ui();
        openOscDetail (page, osc, "map");
        auto incompatible = a;
        incompatible.set ("osc1.type", 1);
        incompatible.apply ("Osc A changes oscillator type", inst);
        page.update();
        check (page.stack.depth() == 1 && osc.typeRaw() == 1, "a preset with a different oscillator type closes the incompatible Frame Map");

        a.apply ("Osc A detail A", inst);
        page.update();
        openOscDetail (page, osc, "map");
        incompatible = a;
        incompatible.set ("osc1.on", 0);
        incompatible.apply ("Osc A powers off", inst);
        page.update();
        check (page.stack.depth() == 1 && ! page.as<pages::OscPage>().ui().isCurrentInstanceOn (inst),
               "a preset that powers Osc A off closes its previous graph detail");
    }

    auto oscPlot = [] (Page& p) { return p.as<pages::OscPage>().ui().ui().displayRect().inset (14); };
    auto playOscPlot = [] (Page& p) { return p.as<PlayView>().oscUi()->displayRect().inset (14); };
    auto expandOsc = [] (Page& p) { p.tap (p.as<PlayView>().oscUi()->compact().full.rect()); };

    for (int n = 2; n <= 3; ++n)
    {
        a = {}; b = {};
        const auto prefix = "osc" + std::to_string (n);
        a.set ((prefix + ".on").c_str(), 1);
        b.set ((prefix + ".on").c_str(), 1);
        a.set ((prefix + ".wavetable").c_str(), 0);
        b.set ((prefix + ".wavetable").c_str(), 1);
        const std::string label = std::string ("Osc ") + (n == 2 ? "B" : "C") + " wavetable";
        roundTrip (label.c_str(), n == 2 ? "oscb" : "oscc", a, b, root, oscPlot);
        roundTrip (("Play expanded " + label).c_str(), "play1", a, b, expandOsc, playOscPlot, n + 1);
    }

    // Two asymmetric audio fixtures keep graph changes clear with no note,
    // transport or live readout. Only the preset's content reference changes.
    synthyboy::MemoryContentStore store;
    constexpr int rate = 44100, frames = 8192;
    for (int i = 0; i < 2; ++i)
    {
        std::vector<float> wave (frames);
        for (int j = 0; j < frames; ++j)
        {
            const double t = static_cast<double> (j) / frames;
            const double amplitude = i == 0 ? 0.12 + t * 0.74 : 0.82 - t * 0.68;
            wave[static_cast<size_t> (j)] = static_cast<float> (amplitude * std::sin (6.283185307 * (i == 0 ? 53 : 173) * t));
        }
        const std::string wav = i == 0 ? "a.wav" : "b.wav";
        const std::string sfz = "<region> sample=" + wav + " lokey=0 hikey=127 pitch_keycenter=60\n";
        store.add ("fixture", wav, synthyboy::encodeWav (wave.data(), nullptr, frames, rate, 16));
        store.add ("fixture", i == 0 ? "a.sfz" : "b.sfz", std::vector<uint8_t> (sfz.begin(), sfz.end()));
    }
    const char* const modeNames[] { "Sample", "Granular", "Spectral", "Multisample" };
    for (int mode = 1; mode <= 4; ++mode)
    {
        a = {}; b = {};
        for (auto* preset : {&a, &b})
        {
            preset->set ("osc1.on", 1);
            preset->set ("osc1.type", static_cast<float> (mode));
            synthyboy::ContentFileRef ref;
            ref.pack = "fixture";
            ref.file = preset == &a ? "a" : "b";
            if (mode == 4)
            {
                ref.file += ".sfz";
                synthyboy::attachInstrument (preset->content, 1, ref, 1);
            }
            else
            {
                ref.file += ".wav";
                synthyboy::attachSample (preset->content, 1, ref, rate, frames, 1);
            }
        }
        const std::string label = std::string ("Osc A ") + modeNames[mode - 1];
        roundTrip (label.c_str(), "osca", a, b, root, oscPlot, 1, 1, 0, &store);
        roundTrip (("Play expanded " + label).c_str(), "play1", a, b, expandOsc, playOscPlot, 2, 1, 0, &store);
        if (mode == 1)
        {
            checkOscDetail (a, b, "Osc A Sample Region", "region", oscspec::kSampleRegion, &store);
            checkOscDetail (a, b, "Osc A Sample Slice", "slice", oscspec::kSampleSlice, &store);
        }
        else if (mode == 2)
        {
            checkOscDetail (a, b, "Osc A Grain Engine", "grain", oscspec::kGrain, &store);
            checkOscDetail (a, b, "Osc A Grain Window", "window", OscUi::kPageGrain, &store);
            checkOscDetail (a, b, "Osc A Granular Unison", "unison", OscUi::kPageUnison, &store);
        }
        else if (mode == 3)
        {
            checkOscDetail (a, b, "Osc A Spectral Filter", "filter", oscspec::kSpecFilter, &store);
            checkOscDetail (a, b, "Osc A Spectral Scan", "scan", oscspec::kSpecScan, &store);
        }
        else
        {
            auto envA = a;
            auto envB = a;
            envA.set ("osc1.multi.override", 1);
            envB.set ("osc1.multi.override", 1);
            envA.set ("osc1.multi.sustain", 12);
            envB.set ("osc1.multi.sustain", 88);
            checkOscDetail (envA, envB, "Osc A Multisample ENV", "env", OscUi::kPageEnvelope, &store);
        }
    }
    a = {}; b = {};
    a.set ("noise.on", 1);
    b.set ("noise.on", 1);
    a.set ("noise.sample", 0);
    b.set ("noise.sample", 3);
    roundTrip ("Noise generator", "oscnoise", a, b, root, oscPlot);
    roundTrip ("Play expanded Noise generator", "play1", a, b, expandOsc, playOscPlot, 5);
    a.set ("noise.type", 1);
    b.set ("noise.type", 1);
    synthyboy::attachSample (a.content, 4, {"fixture", "a.wav", "", ""}, rate, frames, 1);
    synthyboy::attachSample (b.content, 4, {"fixture", "b.wav", "", ""}, rate, frames, 1);
    a.set ("noise.sample", 0);
    b.set ("noise.sample", 0);
    roundTrip ("Noise sample", "oscnoise", a, b, root, oscPlot, 1, 1, 0, &store);
    roundTrip ("Play expanded Noise sample", "play1", a, b, expandOsc, playOscPlot, 5, 1, 0, &store);

    runFilter (a, b);

    a = {}; b = {};
    a.set ("master.portamento.slope", -7);
    b.set ("master.portamento.slope", 7);
    roundTrip ("Master GLIDE", "master", a, b, root,
               [] (Page& p) { return p.as<pages::MasterPage>().ui().screen().portaCurve.rect().inset (6); });
    a.set ("voicectl.s2.pan", -90);
    b.set ("voicectl.s2.pan", 80);
    roundTrip ("Voice PAN sequence", "voice", a, b, root,
               [] (Page& p) { return p.as<pages::VoicePage>().ui().screen().grid.rect().inset (6); });

    struct FxFixture
    {
        const char* name;
        const char* prefix;
        const char* value;
        int type;
        float a, b;
        bool moreGraph;
    };
    const FxFixture fixtures[] {
        {"EQ", "eq", "eq.mid.gain", synthyboy::kFxTypeEq, -12, 12, true},
        {"Compressor", "compressor", "compressor.mid.upper_threshold", synthyboy::kFxTypeCompressor, -76, -4, false},
        {"Delay", "delay", "delay.feedback", synthyboy::kFxTypeDelay, 5, 85, true},
        {"Filter", "fxfilter", "fxfilter.cutoff", synthyboy::kFxTypeFilter, 190, 7000, true},
        {"Distortion", "distortion", "distortion.drive", synthyboy::kFxTypeDistortion, -18, 24, true},
    };
    for (const auto& fx : fixtures)
    {
        a = {}; b = {};
        const std::string prefix = fx.prefix;
        a.set ((prefix + ".on").c_str(), 1);
        b.set ((prefix + ".on").c_str(), 1);
        a.set (fx.value, fx.a);
        b.set (fx.value, fx.b);
        if (fx.type == synthyboy::kFxTypeDistortion)
        {
            a.set ("distortion.filter_order", 1);
            b.set ("distortion.filter_order", 1);
        }
        const auto plot = [&] (const ui::FxSlotEditor& editor, bool more) {
            if (! editor.hasDisplay() || editor.currentType() != fx.type || editor.isMoreView() != more)
                return ui::Rect {};
            const ui::Rect r = editor.display.rect();
            return ui::Rect { r.x + 44, r.y + 22, r.w - 60, r.h - 46 };
        };
        auto standalone = [] (Page& p) -> ui::FxSlotEditor& { return p.as<pages::FxPage>().ui().screen().slotEditor; };
        const auto peek = [] (Page& p) -> ui::FxSlotEditor* {
            auto* screen = dynamic_cast<ui::FxFullScreen*> (p.as<pages::MixerPage>().ui().peekRoot());
            return screen ? &screen->slotEditor : nullptr;
        };
        const std::string label = std::string ("FX ") + fx.name;
        roundTrip (label.c_str(), "fx", a, b, root, [&] (Page& p) { return plot (standalone (p), false); });
        if (fx.moreGraph)
            roundTrip ((label + " MORE").c_str(), "fx", a, b,
                       [] (Page& p) { p.openFxMore(); },
                       [&] (Page& p) { return plot (standalone (p), true); });
        for (int rack = 0; rack < 3; ++rack)
        {
            a.set ((prefix + ".rack").c_str(), static_cast<float> (rack));
            b.set ((prefix + ".rack").c_str(), static_cast<float> (rack));
            const auto rootName = rack == 0 ? "MAIN PEEK " : rack == 1 ? "BUS 1 PEEK " : "BUS 2 PEEK ";
            const std::string peekLabel = rootName + label;
            roundTrip (peekLabel.c_str(), "mixer", a, b, [rack] (Page& p) { p.openPeek (rack + 2); },
                       [&] (Page& p) { const auto* ed = peek (p); return ed ? plot (*ed, false) : ui::Rect {}; });
            if (fx.moreGraph)
                roundTrip ((peekLabel + " MORE").c_str(), "mixer", a, b, [&] (Page& p) {
                    p.openPeek (rack + 2);
                    p.openFxMore();
                }, [&] (Page& p) { const auto* ed = peek (p); return ed ? plot (*ed, true) : ui::Rect {}; });
        }
    }

    a = {}; b = {};
    a.set ("eq.on", 1);
    a.set ("eq.mid.gain", -10);
    b.set ("distortion.on", 1);
    b.set ("distortion.drive", 23);
    roundTrip ("MAIN PEEK changes module EQ to Distortion", "mixer", a, b,
               [] (Page& p) { p.openPeek (2); },
               [] (Page& p) {
                   const auto* fx = dynamic_cast<ui::FxFullScreen*> (p.as<pages::MixerPage>().ui().peekRoot());
                   const int type = p.inst.get ("eq.on") > 0.5f ? synthyboy::kFxTypeEq : synthyboy::kFxTypeDistortion;
                   if (! fx || ! fx->slotEditor.hasDisplay() || fx->slotEditor.currentType() != type)
                       return ui::Rect {};
                   return fx->slotEditor.display.rect().inset (44);
               });

    a = {};
    a.set ("chorus.on", 1);
    Instance inst;
    a.apply ("Chorus MORE", inst);
    Page chorus ("fx", inst);
    auto& editor = chorus.as<pages::FxPage>().ui().screen().slotEditor;
    chorus.openFxMore();
    check (editor.currentType() == synthyboy::kFxTypeChorus && editor.isMoreView() && ! editor.hasDisplay(),
           "Chorus MORE has its controls and no custom graph in the production layout");
}
} // namespace preset_graph
