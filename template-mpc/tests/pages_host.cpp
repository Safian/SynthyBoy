// MpcKit Demo - host test of the three pages and the kit pieces they use. No display, no VST host: a DemoInstance is driven directly. Built by `cmake --build <dir> --target mpckit_demo_pages_host`.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cstdio>
#include <memory>
#include <string>

#include "../src/demo_instance.h"
#include "../src/demo_page_set.h"
#include "../src/pages/list_page.h"
#include "../src/pages/main_page.h"
#include "../src/pages/shape_page.h"

namespace {

int g_failures = 0;
#define CHECK(cond) do { if (! (cond)) { ++g_failures; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

using synthyboy_mpc::overlay::TouchEvent;
using synthyboy_mpc::overlay::UiStack;
namespace gpu = synthyboy_mpc::overlay::gpu;

/** Draws a page into a scene and returns how many ops it made. */
template <class Page>
uint32_t drawOps (const Page& page)
{
    auto data = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder sb (*data);
    sb.begin (gpu::kKindPlay, 0, 1280, 800);
    page.draw (sb);
    return data->numOps;
}

TouchEvent ev (TouchEvent::Kind k, int x, int y) { TouchEvent e; e.kind = k; e.x = x; e.y = y; return e; }

template <class Page>
void commonChecks (const char* name)
{
    demo::DemoInstance inst;
    UiStack stack;
    stack.openFull (Page::kElementPage);
    Page page;
    page.update (inst, stack, 0.0, mpckit::PageInput {});
    std::printf ("%s: %u ops, Q slot 0 -> %d, groups %d\n", name, drawOps (page), page.qTargets()[0], page.numGroups());
    CHECK (drawOps (page) > 10);
    CHECK (page.qTargets()[0] >= 0);   // the first group is published
    CHECK (page.topPage() == 0);
    for (const mpckit::Binder::Item& it : page.binder().items())   // every key of the page exists in the table
        CHECK (it.param >= 0);
}

} // namespace

int main()
{
    // the parameter table
    const mpckit::IParamSource& p = demo::params();
    CHECK (p.count() == demo::kNumParams);
    CHECK (p.indexOfKey ("cutoff") == demo::kCutoff && p.indexOfKey ("nope") == -1);
    for (int i = 0; i < p.count(); ++i)
    {
        const float plain = p.desc (i).def;
        CHECK (std::fabs (p.fromNormalised (i, p.toNormalised (i, plain)) - plain) <= 1.0e-3f * std::max (1.0f, std::fabs (plain)));
    }
    char text[32];
    p.valueText (demo::kCutoff, 8000.0f, text, sizeof text);
    CHECK (std::string (text) == "8.00 kHz");
    p.valueText (demo::kPan, 0.0f, text, sizeof text);
    CHECK (std::string (text) == "C");
    CHECK (p.stepCount (demo::kMode) == 4 && p.stepCount (demo::kLevel) == 0);

    // the tabs and their colours
    std::string why;
    CHECK (mpckit::validatePages (demo::pageSet(), why));
    if (! why.empty())
        std::printf ("validatePages: %s\n", why.c_str());

    commonChecks<demo::MainPage> ("main");
    commonChecks<demo::ShapePage> ("shape");
    commonChecks<demo::ListPage> ("list");

    // a drag on the Level knob of the Main page changes the parameter
    {
        demo::DemoInstance inst;
        UiStack stack;
        stack.openFull (demo::MainPage::kElementPage);
        demo::MainPage page;
        page.update (inst, stack, 0.0, mpckit::PageInput {});
        const float before = inst.param (demo::kLevel);
        CHECK (page.touch (ev (TouchEvent::Down, 180, 230), stack, inst, 0.0));
        CHECK (page.fingerDown());
        page.touch (ev (TouchEvent::Move, 180, 230), stack, inst, 10.0);
        page.touch (ev (TouchEvent::Move, 180, 150), stack, inst, 20.0);
        page.touch (ev (TouchEvent::Up, 180, 150), stack, inst, 30.0);
        CHECK (inst.param (demo::kLevel) > before);
        CHECK (inst.takeDirty() != 0u);   // the display thread would tell the host
    }

    // the stepper of the List page opens its list, a tap on a row writes the choice
    {
        demo::DemoInstance inst;
        UiStack stack;
        stack.openFull (demo::ListPage::kElementPage);
        demo::ListPage page;
        page.update (inst, stack, 0.0, mpckit::PageInput {});
        CHECK (page.touch (ev (TouchEvent::Down, 270, 110 + 40 + 50), stack, inst, 0.0));   // the middle of the stepper
        CHECK (page.touch (ev (TouchEvent::Up, 270, 110 + 40 + 50), stack, inst, 20.0));
        CHECK (stack.depth() == 2);
        page.update (inst, stack, 30.0, mpckit::PageInput {});
        CHECK (page.topPage() == mpckit::PageBase::kListPage);
        CHECK (page.qTargets()[0] == -1);   // a popup is up: no Q target
    }

    // a preset writes every parameter
    {
        demo::DemoInstance inst;
        inst.presets.select (3);
        CHECK (inst.param (demo::kPitch) == -12.0f);
    }

    std::printf (g_failures == 0 ? "OK\n" : "FAILED: %d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
