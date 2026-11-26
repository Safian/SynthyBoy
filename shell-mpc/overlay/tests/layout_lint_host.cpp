// SynthyBoy - MPC overlay: the automatic LAYOUT LINT. Builds every plugin-drawn page of the overlay build (the views of overlay_layout.h) in the states of the UI gallery (Init and every factory preset;
// sub-pages, detail pages, every FX effect type and page, both Macro banks, Master's two sides, the LFO modes, the editor pages) and checks the component tree and the scene of each:
//   a  two interactive controls (leaves of the tree) overlap
//   b  a touch rectangle smaller than 56 px in one direction
//   c  a control (or a Q / modulation chip) not fully inside the drawing region (the tab, 1280 x 628) or not inside its parent
//   d  text: cut with an ellipsis, wider than its cache cell or than the control it is drawn in, outside the drawing region, two texts overlapping
//   e  a Q chip / modulation tab covers the arc or the handle of a knob
//   f  the same id (= the same engine parameter) on two interactive controls of one page
//   g  a titled block with less than half of its area covered by its children
//   h  more than three distinct knob diameters on one page
// Exit code 0 only when every finding is in the baseline file (layout_lint_baseline.json) and, in a run over all presets, every baseline entry still occurs. --self-test checks the checks.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csetjmp>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cxxabi.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <typeinfo>
#include <unistd.h>
#include <vector>

#include "../../ui/ui.h"
#include "pages.h"
#include "play_view.h"
#include "synthyboy/filter_response.h"
#include "synthyboy/fx_rack.h"
#include "synthyboy/fx_types.h"
#include "synthyboy/modulation.h"
#include "synthyboy/osc_cycle.h"
#include "synthyboy/params.h"
#include "synthyboy/preset.h"
#include "synthyboy/state.h"

namespace fs = std::filesystem;
using namespace synthyboy_mpc;
using namespace synthyboy_mpc::overlay;

#ifndef LINT_SOURCE_DIR
#define LINT_SOURCE_DIR "."
#endif

namespace
{

// =============================================================================================================================================================
// findings

struct Finding
{
    std::string page, state, check, key, a, b, detail;
    int severity = 3;   // 1 worst (overlap, cut text) .. 4
};

struct Agg
{
    Finding f;
    std::set<std::string> presets;
    std::string firstPreset;
};

const char* kRegionName = "region";
constexpr int kMinTouch = 56;

struct FRect
{
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    float w() const { return x1 - x0; }
    float h() const { return y1 - y0; }
};

std::string demangled (const std::type_info& ti)
{
    int st = 0;
    char* n = abi::__cxa_demangle (ti.name(), nullptr, nullptr, &st);
    std::string s = (st == 0 && n) ? n : ti.name();
    std::free (n);
    const size_t p = s.rfind ("::");
    return p == std::string::npos ? s : s.substr (p + 2);
}

std::string maskDigits (const std::string& s)
{
    std::string o = s;
    for (char& c : o)
        if (c >= '0' && c <= '9')
            c = '#';
    for (char& c : o)
        if (c == '"' || c == '\\' || c == '|' || c == '\n')
            c = '_';
    return o;
}

std::string gridOf (float x, float y) { return std::to_string (static_cast<int> (x) / 16 * 16) + "," + std::to_string (static_cast<int> (y) / 16 * 16); }

std::string rectStr (const ui::Rect& r)
{
    char b[64];
    std::snprintf (b, sizeof b, "%d,%d %dx%d", r.x, r.y, r.w, r.h);
    return b;
}
std::string frectStr (const FRect& r)
{
    char b[80];
    std::snprintf (b, sizeof b, "%.0f,%.0f %.0fx%.0f", static_cast<double> (r.x0), static_cast<double> (r.y0), static_cast<double> (r.w()), static_cast<double> (r.h()));
    return b;
}

// =============================================================================================================================================================
// what a page gives the analysis: the component tree (roots), the scene

struct TextOp
{
    std::string s;
    FRect box, cell;
    float size = 0;
    bool hasCell = false;
    int owner = -1;   // the leaf the text belongs to (smallest leaf rectangle that contains the centre of the text), -1 none
    bool inChip = false;   // the text of a Q chip / modulation tab (it belongs to the chip, not to the control under it)
};
struct Chip
{
    FRect r;
    bool tab = false;   // a modulation tab (else a Q index chip)
};
struct BlockOp { FRect r; };

struct Capture
{
    std::string page, state;
    ui::Rect region { 0, 110, 1280, 628 };
    std::vector<ui::Component*> roots;
    bool sceneOnly = false;   // no component tree (the Play view): only the checks that need the scene
    std::vector<TextOp> texts;
    std::vector<Chip> chips;
    std::vector<BlockOp> blocks;
    std::vector<FRect> cells;   // the cache cells of the scene (without their 2 px pad): what the components and the displays say they draw in
    bool popup = false;         // a popup over the page (route panel): the page under it is in the scene but not in the tree
    bool overflow = false;
    int numOps = 0;
};

void parseScene (const gpu::SceneData& d, Capture& cap)
{
    const ui::theme::Look& T = ui::theme::L();
    bool inCell = false;
    FRect cell;
    for (uint32_t i = 0; i < d.numOps; ++i)
    {
        const gpu::SceneOp& o = d.ops[i];
        if (o.op == gpu::kOpCellBegin)
        {
            inCell = true;
            cell = { o.f[0], o.f[1], o.f[2], o.f[3] };
            cap.cells.push_back ({ o.f[0] + 2.0f, o.f[1] + 2.0f, o.f[2] - 2.0f, o.f[3] - 2.0f });
        }
        else if (o.op == gpu::kOpCellEnd) inCell = false;
        else if (o.op == gpu::kOpText)
        {
            TextOp t;
            const size_t off = static_cast<size_t> (o.f[3]), len = static_cast<size_t> (o.f[4]);
            if (off + len > static_cast<size_t> (gpu::kMaxText))
                continue;
            t.s.assign (d.text + off, len);
            t.size = o.f[2];
            const float tr = gpu::bitsFloat (o.rgba2);
            const float w = sdf::textWidth (d.text + off, static_cast<int> (len), t.size, tr);
            bool desc = false;
            for (char c : t.s)
                desc = desc || std::strchr ("gjpqy,;Q", c) != nullptr;
            t.box = { o.f[0], o.f[1] - sdf::capHeight (t.size), o.f[0] + w, o.f[1] + (desc ? 0.2f * t.size : 0.0f) };
            t.hasCell = inCell;
            t.cell = cell;
            cap.texts.push_back (t);
        }
        else if (o.op == gpu::kOpRRect)
        {
            const FRect r { o.f[0], o.f[1], o.f[2], o.f[3] };
            const float radius = o.f[4];
            const bool fill = o.rgba2 == 0;
            if (fill && std::fabs (radius - T.rBlock) < 0.01f)
                cap.blocks.push_back ({ r });
            const bool qSig = std::fabs (radius - 5.0f) < 0.01f && std::fabs (r.h() - 17.0f) < 0.01f && (std::fabs (r.w() - 18.0f) < 0.01f || std::fabs (r.w() - 26.0f) < 0.01f);
            const bool tabSig = std::fabs (radius - 4.0f) < 0.01f && r.h() <= 18.01f && r.w() >= 35.99f && r.w() <= 56.01f;
            if (qSig || tabSig)
            {
                // a chip is a rounded rectangle followed by its text (a digit, a source name) centred in it
                for (uint32_t j = i + 1; j < d.numOps && j <= i + 3; ++j)
                    if (d.ops[j].op == gpu::kOpText)
                    {
                        const float cx = d.ops[j].f[0], cy = d.ops[j].f[1];
                        if (cx >= r.x0 - 1 && cx <= r.x1 + 1 && cy >= r.y0 && cy <= r.y1 + 1)
                        {
                            bool dup = false;
                            for (const Chip& c : cap.chips)
                                dup = dup || (std::fabs (c.r.x0 - r.x0) < 0.01f && std::fabs (c.r.y0 - r.y0) < 0.01f && std::fabs (c.r.x1 - r.x1) < 0.01f);
                            if (! dup)
                                cap.chips.push_back ({ r, tabSig && ! qSig });
                        }
                        break;
                    }
            }
        }
    }
    cap.numOps = static_cast<int> (d.numOps);
    cap.overflow = d.numOps >= static_cast<uint32_t> (gpu::kMaxOps);
}

// =============================================================================================================================================================
// the tree

struct Leaf
{
    ui::Component* c = nullptr;
    ui::Rect r;
    std::string kind, name;
    bool interactive = true;
    int id = 0;
    std::vector<ui::Rect> targets;
};
struct Rel { ui::Rect child, parent; std::string childName, parentName; };

const std::set<std::string>& displayKinds()
{
    static const std::set<std::string> k { "SpectrumStrip", "PortamentoCurveDisplay", "FxDisplay", "ListPopup", "FilterOutReadout", "ScalarUsedByList" };
    return k;
}

std::string leafName (ui::Component* c, const std::string& kind)
{
    std::string n = kind;
    if (c->id() != 0)
        return n + "#" + std::to_string (c->id());
    if (auto* k = dynamic_cast<ui::Knob*> (c))
        n += "[" + maskDigits (k->state.label) + "]";
    else if (auto* s = dynamic_cast<ui::Stepper*> (c))
        n += "[" + maskDigits (s->state.caption) + "]";
    return n + "@" + gridOf (static_cast<float> (c->rect().x), static_cast<float> (c->rect().y));
}

void walk (ui::Component* c, ui::Composite* parent, std::vector<Leaf>& leaves, std::vector<Rel>& rels, const std::string& parentName)
{
    if (auto* comp = dynamic_cast<ui::Composite*> (c))
    {
        const std::string kind = demangled (typeid (*c));
        const std::string name = leafName (c, kind);
        if (parent)
            rels.push_back ({ c->rect(), parent->rect(), name, parentName });
        for (int i = 0; i < comp->numChildren(); ++i)
            if (comp->childShown (i))
                walk (comp->child (i), comp, leaves, rels, name);
        return;
    }
    Leaf l;
    l.c = c;
    l.r = c->rect();
    l.kind = demangled (typeid (*c));
    l.name = leafName (c, l.kind);
    l.interactive = displayKinds().count (l.kind) == 0;
    l.id = c->id();
    ui::Rect tg[16];
    const int n = c->touchTargets (tg, 16);
    for (int i = 0; i < n; ++i)
        l.targets.push_back (tg[i]);
    if (parent)
        rels.push_back ({ l.r, parent->rect(), l.name, parentName });
    leaves.push_back (l);
}

bool rectOverlap (const ui::Rect& a, const ui::Rect& b) { return a.overlaps (b); }

double unionArea (std::vector<ui::Rect> rs, const ui::Rect& clip)
{
    std::vector<ui::Rect> v;
    for (const ui::Rect& r : rs)
    {
        const int x0 = std::max (r.x, clip.x), y0 = std::max (r.y, clip.y), x1 = std::min (r.right(), clip.right()), y1 = std::min (r.bottom(), clip.bottom());
        if (x1 > x0 && y1 > y0)
            v.push_back ({ x0, y0, x1 - x0, y1 - y0 });
    }
    std::vector<int> xs, ys;
    for (const ui::Rect& r : v) { xs.push_back (r.x); xs.push_back (r.right()); ys.push_back (r.y); ys.push_back (r.bottom()); }
    std::sort (xs.begin(), xs.end()); xs.erase (std::unique (xs.begin(), xs.end()), xs.end());
    std::sort (ys.begin(), ys.end()); ys.erase (std::unique (ys.begin(), ys.end()), ys.end());
    double a = 0;
    for (size_t i = 0; i + 1 < xs.size(); ++i)
        for (size_t j = 0; j + 1 < ys.size(); ++j)
            for (const ui::Rect& r : v)
                if (r.x <= xs[i] && r.right() >= xs[i + 1] && r.y <= ys[j] && r.bottom() >= ys[j + 1])
                {
                    a += static_cast<double> (xs[i + 1] - xs[i]) * (ys[j + 1] - ys[j]);
                    break;
                }
    return a;
}

bool discHitsRect (float cx, float cy, float rad, const FRect& r)
{
    const float nx = std::clamp (cx, r.x0, r.x1), ny = std::clamp (cy, r.y0, r.y1);
    return (cx - nx) * (cx - nx) + (cy - ny) * (cy - ny) <= rad * rad;
}

// =============================================================================================================================================================
// the checks

struct Sink
{
    std::map<std::string, Agg> agg;
    std::string preset;   // the state the current page was built from
    long raw = 0;
    bool stats = false;   // --stats: one line of numbers per page state of the first instance state
    std::set<std::string> problems;   // a page whose tree has no leaves (the lint would see nothing)

    void add (const Capture& cap, const char* check, int severity, const std::string& a, const std::string& b, const std::string& detail)
    {
        Finding f;
        f.page = cap.page;
        f.state = cap.state;
        f.check = check;
        f.severity = severity;
        f.a = a;
        f.b = b;
        f.detail = detail;
        f.key = cap.page + "/" + cap.state + "|" + check + "|" + a + (b.empty() ? "" : "|" + b);
        ++raw;
        auto it = agg.find (f.key);
        if (it == agg.end())
        {
            Agg g;
            g.f = f;
            g.firstPreset = preset;
            it = agg.emplace (f.key, g).first;
        }
        it->second.presets.insert (preset);
    }
};

void analyse (Capture& cap, Sink& sink)
{
    std::vector<Leaf> leaves;
    std::vector<Rel> rels;
    for (ui::Component* root : cap.roots)
        walk (root, nullptr, leaves, rels, "");

    const ui::Rect& R = cap.region;
    auto inRegion = [&] (const ui::Rect& r) { return r.x >= R.x && r.y >= R.y && r.right() <= R.right() && r.bottom() <= R.bottom(); };

    if (! cap.sceneOnly && leaves.empty())
        sink.problems.insert (cap.page + "/" + cap.state + ": the component tree has no leaves (the tree checks saw nothing)");

    if (sink.stats && sink.preset == "Init")
    {
        size_t inter = 0;
        for (const Leaf& L : leaves) inter += L.interactive;
        std::printf ("stat %-9s %-18s leaves %3zu (interactive %3zu) rels %3zu texts %3zu blocks %2zu chips %2zu cells %3zu\n", cap.page.c_str(), cap.state.c_str(), leaves.size(), inter, rels.size(), cap.texts.size(), cap.blocks.size(), cap.chips.size(), cap.cells.size());
    }

    // owners of the texts
    for (TextOp& t : cap.texts)
    {
        const float cx = (t.box.x0 + t.box.x1) * 0.5f, cy = (t.box.y0 + t.box.y1) * 0.5f;
        long best = -1;
        for (size_t i = 0; i < leaves.size(); ++i)
        {
            const ui::Rect& r = leaves[i].r;
            if (cx >= r.x && cx < r.right() && cy >= r.y && cy < r.bottom() && (best < 0 || static_cast<long> (r.w) * r.h < static_cast<long> (leaves[static_cast<size_t> (best)].r.w) * leaves[static_cast<size_t> (best)].r.h))
                best = static_cast<long> (i);
        }
        t.owner = static_cast<int> (best);
        for (const Chip& ch : cap.chips)
            if (cx >= ch.r.x0 - 1 && cx <= ch.r.x1 + 1 && cy >= ch.r.y0 - 1 && cy <= ch.r.y1 + 1)
                t.inChip = true;
    }

    // ---- a / b / c / f / h: the tree
    if (! cap.sceneOnly)
    {
        std::vector<size_t> act;
        for (size_t i = 0; i < leaves.size(); ++i)
            if (leaves[i].interactive && ! leaves[i].r.empty())
                act.push_back (i);

        // a: overlaps of interactive controls
        for (size_t x = 0; x < act.size(); ++x)
            for (size_t y = x + 1; y < act.size(); ++y)
            {
                const Leaf &A = leaves[act[x]], &B = leaves[act[y]];
                if (rectOverlap (A.r, B.r))
                {
                    const int ox = std::min (A.r.right(), B.r.right()) - std::max (A.r.x, B.r.x), oy = std::min (A.r.bottom(), B.r.bottom()) - std::max (A.r.y, B.r.y);
                    std::string a = A.name, b = B.name;
                    if (b < a) std::swap (a, b);
                    sink.add (cap, "a", 1, a, b, A.name + " (" + rectStr (A.r) + ") x " + B.name + " (" + rectStr (B.r) + "): overlap " + std::to_string (ox) + " x " + std::to_string (oy) + " px");
                }
            }

        // b: touch rectangles under 56 px (no component declares an exception: every violation is reported)
        for (size_t i : act)
        {
            const Leaf& L = leaves[i];
            for (const ui::Rect& t : L.targets)
                if (! t.empty() && (t.w < kMinTouch || t.h < kMinTouch))
                    sink.add (cap, "b", 3, L.name, "", L.name + " touch rectangle " + std::to_string (t.w) + " x " + std::to_string (t.h) + " px (" + rectStr (t) + ")");
        }

        // c: inside the drawing region and inside the parent
        for (const Leaf& L : leaves)
            if (! L.r.empty() && ! inRegion (L.r))
                sink.add (cap, "c", 1, L.name, kRegionName, L.name + " (" + rectStr (L.r) + ") is not inside the drawing region " + rectStr (R));
        for (ui::Component* root : cap.roots)
            if (! inRegion (root->rect()))
                sink.add (cap, "c", 1, demangled (typeid (*root)), kRegionName, "root " + demangled (typeid (*root)) + " (" + rectStr (root->rect()) + ") is not inside the drawing region " + rectStr (R));
        for (const Rel& r : rels)
            if (! r.child.empty() && ! r.parent.contains (r.child))
                sink.add (cap, "c", 2, r.childName, r.parentName, r.childName + " (" + rectStr (r.child) + ") is not inside its parent " + r.parentName + " (" + rectStr (r.parent) + ")");

        // f: one id (one engine parameter) on two interactive controls
        std::map<int, std::vector<size_t>> byId;
        for (size_t i : act)
            if (leaves[i].id != 0)
            {
                // the one declared exception: the "+" cards of the FX chain (the add card and the faint free places after it) all are the same add button and share one id (FxChainBar::assignIds)
                if (auto* card = dynamic_cast<ui::FxSlotCard*> (leaves[i].c))
                    if (card->state.isPlus)
                        continue;
                byId[leaves[i].id].push_back (i);
            }
        for (auto& kv : byId)
            if (kv.second.size() > 1)
            {
                const Leaf &A = leaves[kv.second[0]], &B = leaves[kv.second[1]];
                std::string a = A.kind + "#" + std::to_string (kv.first), b = B.kind;
                sink.add (cap, "f", 2, a, b, std::to_string (kv.second.size()) + " controls share id " + std::to_string (kv.first) + ": " + A.kind + " (" + rectStr (A.r) + ") and " + B.kind + " (" + rectStr (B.r) + ")");
            }

        // h: knob diameters
        std::map<int, int> dia;
        for (const Leaf& L : leaves)
            if (auto* k = dynamic_cast<ui::Knob*> (L.c))
            {
                if (L.r.empty()) continue;
                const ui::Knob::Geom g = k->geom();
                ++dia[static_cast<int> (std::lround (2.0f * g.radius + g.stroke + 6.0f))];
            }
        if (dia.size() > 3)
        {
            std::string d;
            for (auto& kv : dia) d += (d.empty() ? "" : ", ") + std::to_string (kv.first) + "px x" + std::to_string (kv.second);
            sink.add (cap, "h", 4, "knob sizes", "", std::to_string (dia.size()) + " distinct knob diameters: " + d);
        }

        // e: chips over the arc / handle of a knob
        for (const Chip& ch : cap.popup ? std::vector<Chip>() : cap.chips)
            for (const Leaf& L : leaves)
            {
                auto* k = dynamic_cast<ui::Knob*> (L.c);
                if (! k || L.r.empty()) continue;
                const ui::Knob::Geom g = k->geom();
                const FRect cr = ch.r;
                if (cr.x1 < L.r.x - 2 || cr.x0 > L.r.right() + 2 || cr.y1 < L.r.y - 20 || cr.y0 > L.r.bottom() + 2) continue;
                const float half = std::max (g.stroke, g.track) * 0.5f;
                bool arc = false;
                for (int a = 0; a <= 270 && ! arc; a += 3)
                {
                    const float ang = ui::Knob::kStartAngle + static_cast<float> (a) * ui::kPi / 180.0f;
                    const float px = g.cx + g.radius * std::cos (ang), py = g.cy + g.radius * std::sin (ang);
                    arc = discHitsRect (px, py, half, cr);
                }
                float vx, vy;
                k->valuePoint (vx, vy);
                const bool handle = discHitsRect (vx, vy, std::max (2.5f, g.stroke * 0.9f), cr);
                bool modh = false;
                if (k->modulated())
                {
                    float mx, my;
                    k->modPoint (mx, my);
                    modh = discHitsRect (mx, my, std::max (2.0f, g.stroke * 0.7f), cr);
                }
                if (arc || handle || modh)
                {
                    const std::string cn = ch.tab ? "tab" : "qchip";
                    sink.add (cap, "e", 2, cn + "@" + gridOf (cr.x0, cr.y0), L.name, std::string (ch.tab ? "modulation tab " : "Q chip ") + frectStr (cr) + " covers the " + (handle ? "handle" : (modh ? "modulation marker" : "arc")) + " of " + L.name + " (" + rectStr (L.r) + ", dial centre " + std::to_string (static_cast<int> (g.cx)) + "," + std::to_string (static_cast<int> (g.cy)) + " r " + std::to_string (static_cast<int> (g.radius)) + ")");
                }
            }
        // e (lane): a Q chip sits in the lane above its member; it must not cover another interactive control
        for (const Chip& ch : cap.popup ? std::vector<Chip>() : cap.chips)
        {
            if (ch.tab) continue;
            const float mx = ch.r.x0 - 4.0f + 1.0f, my = ch.r.y0 + 15.0f + 1.0f;   // the member's top left corner (Pen::qIndex)
            for (size_t i : act)
            {
                const ui::Rect& r = leaves[i].r;
                const bool own = mx >= r.x && mx < r.right() && my >= r.y && my < r.bottom();
                const float ox = std::min (ch.r.x1, static_cast<float> (r.right())) - std::max (ch.r.x0, static_cast<float> (r.x)), oy = std::min (ch.r.y1, static_cast<float> (r.bottom())) - std::max (ch.r.y0, static_cast<float> (r.y));
                if (! own && ox > 0.5f && oy > 0.5f)
                    sink.add (cap, "e", 2, "qchip@" + gridOf (ch.r.x0, ch.r.y0), leaves[i].name, "Q chip " + frectStr (ch.r) + " covers the control " + leaves[i].name + " (" + rectStr (r) + ") by " + std::to_string (static_cast<int> (ox)) + " x " + std::to_string (static_cast<int> (oy)) + " px");
            }
        }
        // c (chips): a chip cut by the region
        for (const Chip& ch : cap.chips)
            if (ch.r.x0 < R.x - 0.01f || ch.r.y0 < R.y - 0.01f || ch.r.x1 > R.right() + 0.01f || ch.r.y1 > R.bottom() + 0.01f)
                sink.add (cap, "c", 1, std::string (ch.tab ? "tab" : "qchip") + "@" + gridOf (ch.r.x0, ch.r.y0), kRegionName, std::string (ch.tab ? "modulation tab " : "Q chip ") + frectStr (ch.r) + " is not inside the drawing region " + rectStr (R));

        // g: blocks with a title that are more than half empty. Content = the leaves (controls, displays) inside the block and the cache cells of the scene inside it that are not the rectangle of a composite (the displays
        // that a page draws itself, outside the tree, draw in cells of their own); the ratio is the union of those rectangles over the area of the block
        std::vector<ui::Rect> compRects;
        for (const Rel& r : rels) compRects.push_back (r.child);
        for (ui::Component* root : cap.roots) compRects.push_back (root->rect());
        for (const BlockOp& b : cap.popup ? std::vector<BlockOp>() : cap.blocks)
        {
            const TextOp* title = nullptr;
            for (const TextOp& t : cap.texts)
                if (t.owner < 0 && ! t.inChip && t.box.x0 >= b.r.x0 && t.box.x1 <= b.r.x1 && t.box.y0 >= b.r.y0 && t.box.y1 <= b.r.y1 && t.box.y1 - b.r.y0 < 34.0f && t.box.x0 - b.r.x0 < 28.0f && t.size >= ui::theme::L().fSmall - 0.01f)
                    if (! title || t.box.y0 < title->box.y0)
                        title = &t;
            if (! title || b.r.w() < 40 || b.r.h() < 40)
                continue;
            const ui::Rect br { static_cast<int> (std::lround (b.r.x0)), static_cast<int> (std::lround (b.r.y0)), static_cast<int> (std::lround (b.r.w())), static_cast<int> (std::lround (b.r.h())) };
            const ui::Rect big = br.inset (-4);
            std::vector<ui::Rect> kids;
            int nLeaves = 0;
            for (const Leaf& L : leaves)
                if (! L.r.empty() && big.contains (L.r))
                {
                    kids.push_back (L.r);
                    ++nLeaves;
                }
            for (const FRect& c : cap.cells)
            {
                bool isChip = false;   // the cell of a Q chip (QGroup::chip) is a chip, not content of the block
                for (const Chip& ch : cap.chips)
                    isChip = isChip || (std::fabs (ch.r.x0 - c.x0) < 0.6f && std::fabs (ch.r.y0 - c.y0) < 0.6f && std::fabs (ch.r.x1 - c.x1) < 0.6f && std::fabs (ch.r.y1 - c.y1) < 0.6f);
                if (isChip)
                    continue;
                const ui::Rect cr { static_cast<int> (std::lround (c.x0)), static_cast<int> (std::lround (c.y0)), static_cast<int> (std::lround (c.w())), static_cast<int> (std::lround (c.h())) };
                bool comp = false;
                for (const ui::Rect& k : compRects)
                    comp = comp || (std::abs (k.x - cr.x) <= 3 && std::abs (k.y - cr.y) <= 3 && std::abs (k.w - cr.w) <= 6 && std::abs (k.h - cr.h) <= 6);
                if (! comp && ! cr.empty() && big.contains (cr))
                    kids.push_back (cr);
            }
            const double ratio = unionArea (kids, br) / (static_cast<double> (br.w) * br.h);
            if (ratio < 0.5)
            {
                char rs[48];
                std::snprintf (rs, sizeof rs, "%.0f %%", ratio * 100.0);
                sink.add (cap, "g", 3, "block[" + maskDigits (title->s) + "]@" + gridOf (b.r.x0, b.r.y0), "", "block \"" + title->s + "\" (" + frectStr (b.r) + ") is covered " + rs + " by its " + std::to_string (nLeaves) + " controls and " + std::to_string (kids.size() - static_cast<size_t> (nLeaves)) + " drawn parts");
            }
        }
    }

    // ---- d: the texts
    for (size_t i = 0; i < cap.texts.size(); ++i)
    {
        const TextOp& t = cap.texts[i];
        const std::string tn = "text[" + maskDigits (t.s) + "]@" + gridOf (t.box.x0, t.box.y0);
        // an ellipsis at the END of a text is a cut (a finding); an ellipsis in the MIDDLE is the rule for the name of a piece of content (ui/content_name.h: a file name is shown by title or stem and loses its middle, never its end)
        if (t.s.size() >= 3 && t.s.compare (t.s.size() - 3, 3, "\xE2\x80\xA6") == 0)
            sink.add (cap, "d", 2, tn, "ellipsis", "\"" + t.s + "\" is cut with an ellipsis (" + frectStr (t.box) + ")");
        if (t.hasCell && (t.box.x0 < t.cell.x0 - 0.5f || t.box.x1 > t.cell.x1 + 0.5f || t.box.y0 < t.cell.y0 - 0.5f || t.box.y1 > t.cell.y1 + 0.5f))
            sink.add (cap, "d", 2, tn, "cell", "\"" + t.s + "\" " + frectStr (t.box) + " leaves its cache cell " + frectStr (t.cell));
        if (! cap.sceneOnly)
        {
            if (t.owner >= 0 && ! t.inChip && ! cap.popup)
            {
                const Leaf& L = leaves[static_cast<size_t> (t.owner)];
                if (t.box.x0 < L.r.x - 1.0f || t.box.x1 > L.r.right() + 1.0f || t.box.y0 < L.r.y - 1.0f || t.box.y1 > L.r.bottom() + 1.0f)
                    sink.add (cap, "d", 1, tn, L.name, "\"" + t.s + "\" " + frectStr (t.box) + " (width " + std::to_string (static_cast<int> (t.box.w())) + ") sticks out of " + L.name + " (" + rectStr (L.r) + ")");
            }
            if (t.box.x0 < R.x - 0.5f || t.box.y0 < R.y - 0.5f || t.box.x1 > R.right() + 0.5f || t.box.y1 > R.bottom() + 0.5f)
                sink.add (cap, "d", 1, tn, kRegionName, "\"" + t.s + "\" " + frectStr (t.box) + " is not inside the drawing region " + rectStr (R));
        }
        for (size_t j = i + 1; j < cap.texts.size() && ! cap.popup; ++j)
        {
            const TextOp& u = cap.texts[j];
            const float ox = std::min (t.box.x1, u.box.x1) - std::max (t.box.x0, u.box.x0), oy = std::min (t.box.y1, u.box.y1) - std::max (t.box.y0, u.box.y0);
            if (ox > 1.5f && oy > 1.5f && ! (t.s == u.s && std::fabs (t.box.x0 - u.box.x0) < 0.6f && std::fabs (t.box.y0 - u.box.y0) < 0.6f))
            {
                const std::string un = "text[" + maskDigits (u.s) + "]@" + gridOf (u.box.x0, u.box.y0);
                std::string a = tn, b = un;
                if (b < a) std::swap (a, b);
                sink.add (cap, "d", 2, a, b, "\"" + t.s + "\" " + frectStr (t.box) + " overlaps \"" + u.s + "\" " + frectStr (u.box) + " (" + std::to_string (static_cast<int> (ox)) + " x " + std::to_string (static_cast<int> (oy)) + " px)");
            }
        }
    }
}

// =============================================================================================================================================================
// the instance (as the gallery's)

class LintInst final : public IInstanceView
{
public:
    LintInst()
    {
        const synthyboy::ParamInfo* t = synthyboy::paramTable();
        for (size_t i = 0; i < synthyboy::paramCount(); ++i)
            v_[i] = t[i].def;
        haveLive = true;
        liveValues.stripPeak[0] = 0.5f;
        liveValues.stripPeak[1] = 0.72f;
        liveValues.stripPeak[2] = 0.64f;
        liveValues.stripPeak[5] = 0.83f;
        liveValues.stripPeak[6] = 0.5f;
        liveValues.stripPeak[7] = 0.3f;
        liveValues.stripPeak[9] = 0.78f;
        liveValues.activeVoices = 4;
        for (float& s : liveValues.voiceStep)
            s = -1.0f;
        liveValues.voiceStep[0] = 2.0f;
        liveValues.voiceStep[1] = 3.0f;
        for (int i = 0; i < synthyboy::LiveValues::kNumEnvs; ++i)
        {
            liveValues.envFineStage[i] = 1;
            liveValues.envFineFraction[i] = 0.35f;
        }
        for (int i = 0; i < synthyboy::LiveValues::kNumLfos; ++i)
            liveValues.lfoPhase[i] = 0.4f;
        liveValues.source[0] = 0.7f;
        liveValues.source[6] = 0.6f;
        liveValues.source[7] = -0.3f;
    }
    float param (int i) const noexcept override { return (i >= 0 && static_cast<size_t> (i) < synthyboy::kMaxParams) ? v_[i] : 0.0f; }
    bool live (synthyboy::LiveValues& out) const noexcept override { out = liveValues; return haveLive; }
    double sampleRate() const noexcept override { return 48000.0; }
    double bpm() const noexcept override { return 120.0; }
    void writeParam (int i, float norm) noexcept override
    {
        if (i >= 0 && static_cast<size_t> (i) < synthyboy::kMaxParams)
        {
            const synthyboy::ParamInfo& p = synthyboy::paramTable()[i];
            v_[i] = synthyboy::constrainValue (p, synthyboy::fromNormalised (p, norm));
        }
    }
    void setQGroup (const int* t, int n) noexcept override { for (int i = 0; i < 4; ++i) q[i] = i < n ? t[i] : -1; }
    void setUiFullOpen (bool open) noexcept override { fullOpen = open; }
    int playSelection (int group) const noexcept override { return (group >= 0 && group < 3) ? playSel[group] : 1; }
    void setPlaySelection (int group, int inst) noexcept override { if (group >= 0 && group < 3) playSel[group] = inst; }
    int playLastSelector() const noexcept override { return 2; }
    int tabView (int what) const noexcept override { return what >= 0 && what < 3 ? tabViewState[what] : 0; }
    void setTabView (int what, int value) noexcept override { if (what >= 0 && what < 3) tabViewState[what] = value; }
    std::shared_ptr<const synthyboy::StateContent> content() const override { return content_; }

    static int idx (const std::string& key)
    {
        const auto* p = synthyboy::findParamByKey (key.c_str());
        return p ? static_cast<int> (p - synthyboy::paramTable()) : -1;
    }
    float get (const std::string& key) const { const int i = idx (key); return i >= 0 ? v_[i] : 0.0f; }
    void set (const std::string& key, float v) { const int i = idx (key); if (i >= 0) v_[i] = v; }
    void mod (int n, int source, const std::string& destKey, float amount, bool bipolar = false)
    {
        char k[24];
        std::snprintf (k, sizeof k, "mod%02d.", n);
        set (std::string (k) + "source", static_cast<float> (source));
        set (std::string (k) + "destination", static_cast<float> (synthyboy::modDestinationChoiceOf (static_cast<size_t> (idx (destKey)))));
        set (std::string (k) + "amount", amount);
        set (std::string (k) + "bipolar", bipolar ? 1.0f : 0.0f);
        set (std::string (k) + "bypass", 0.0f);
    }
    bool loadPresetFile (const std::string& path)
    {
        std::ifstream f (path, std::ios::binary);
        if (! f.is_open())
            return false;
        std::string str ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
        synthyboy::PresetMeta meta;
        synthyboy::ParamValues values;
        synthyboy::StateContent content;
        synthyboy::LoadResult res = synthyboy::loadPreset (str.data(), str.size(), meta, values, content);
        if (res.status == synthyboy::LoadStatus::invalid)
            return false;
        for (size_t i = 0; i < synthyboy::paramCount(); ++i)
            v_[i] = values.plain[i];
        if (res.content == synthyboy::ContentStatus::ok)
            content_ = std::make_shared<synthyboy::StateContent> (std::move (content));
        return true;
    }
    void setContent (std::shared_ptr<const synthyboy::StateContent> c) { content_ = std::move (c); }   // (a content the lint puts in: the NOTE / VELO curves)
    int q[4] = { -1, -1, -1, -1 };
    int playSel[3] = { 2, 1, 1 };
    int tabViewState[3] = { 0, 0, 0 };   // mpcTabView: the random LFO shown, ...
    bool fullOpen = false, haveLive = true;
    synthyboy::LiveValues liveValues;

private:
    float v_[synthyboy::kMaxParams] = {};
    std::shared_ptr<const synthyboy::StateContent> content_;
};

// =============================================================================================================================================================
// running a page

struct Runner
{
    Sink sink;
    bool render = false;
    long pages = 0, ops = 0;
    std::vector<std::string> problems;   // crashes, exceptions, single colour pages, overflowing scenes
    std::unique_ptr<gpu::SceneData> scene = std::make_unique<gpu::SceneData>();
    std::string preset;
};

sigjmp_buf g_jb;
volatile sig_atomic_t g_guard = 0;
void onSignal (int s)
{
    if (g_guard)
        siglongjmp (g_jb, s);
    _exit (128 + s);
}

template <class Ui>
struct Rig
{
    Ui ui;
    LintInst inst;
    UiStack stack;
    page::Input in;
    double now = 1000.0;

    Rig (const LintInst& base, int instance) : inst (base)
    {
        in.instance = instance;
        stack.openFull (Ui::kElementPage);
    }
    void refresh (int times = 2)
    {
        for (int i = 0; i < times; ++i)
        {
            now += 150.0;
            in.live = inst.haveLive ? &inst.liveValues : nullptr;
            ui.update (inst, stack, now, in);
        }
    }
    void tapAt (int x, int y, int holdMs = 40)
    {
        TouchEvent e; e.kind = TouchEvent::Down; e.x = x; e.y = y;
        ui.touch (e, stack, inst, now);
        ui.tick (now + holdMs, stack, inst);
        e.kind = TouchEvent::Up;
        ui.touch (e, stack, inst, now + holdMs);
        refresh();
    }
    void tap (const ui::Component& c) { tapAt (c.rect().x + c.rect().w / 2, c.rect().y + c.rect().h / 2); }
};

/** Draws the scene of the page with `draw`, parses it, checks it. */
void lintPage (Runner& run, const std::string& page, const std::string& state, const std::function<void (gpu::SceneBuilder&)>& draw, std::vector<ui::Component*> roots, bool sceneOnly = false, bool popup = false, int w = 1280, int h = 800)
{
    Capture cap;
    cap.popup = popup;
    cap.page = page;
    cap.state = state;
    cap.roots = std::move (roots);
    cap.sceneOnly = sceneOnly;
    gpu::SceneBuilder sb (*run.scene);
    sb.begin (gpu::kKindPlay, 0, w, h);
    draw (sb);
    parseScene (*run.scene, cap);
    ++run.pages;
    run.ops += cap.numOps;
    if (cap.overflow || sb.overflow)
        run.problems.push_back (page + "/" + state + " [" + run.preset + "]: the scene overflowed the op or text pool (" + std::to_string (cap.numOps) + " ops)");
    if (run.render)
    {
        std::vector<uint32_t> px;
        renderScene (*run.scene, px);
        std::set<uint32_t> distinct;
        for (size_t i = 0; i < px.size() && distinct.size() < 3; i += 7)
            distinct.insert (px[i]);
        if (distinct.size() < 2)
            run.problems.push_back (page + "/" + state + " [" + run.preset + "]: renders a single colour");
    }
    analyse (cap, run.sink);
}

template <class Ui>
void lintRig (Runner& run, Rig<Ui>& r, const std::string& page, const std::string& state, std::vector<ui::Component*> roots, bool popup = false)
{
    lintPage (run, page, state, [&] (gpu::SceneBuilder& sb) { r.ui.draw (sb); }, std::move (roots), false, popup);
}

/** Runs `fn` guarded: an exception or a signal (a crash) is a problem of the page, not the end of the lint. */
void guarded (Runner& run, const std::string& what, const std::function<void()>& fn)
{
    struct sigaction sa {}, oldSegv {}, oldBus {}, oldAbrt {}, oldFpe {}, oldIll {};
    sa.sa_handler = onSignal;
    sigemptyset (&sa.sa_mask);
    sa.sa_flags = SA_NODEFER;
    sigaction (SIGSEGV, &sa, &oldSegv);
    sigaction (SIGBUS, &sa, &oldBus);
    sigaction (SIGABRT, &sa, &oldAbrt);
    sigaction (SIGFPE, &sa, &oldFpe);
    sigaction (SIGILL, &sa, &oldIll);
    int sig = 0;
    g_guard = 1;
    if ((sig = sigsetjmp (g_jb, 1)) == 0)
    {
        try { fn(); }
        catch (const std::exception& e) { run.problems.push_back (what + " [" + run.preset + "]: exception " + e.what()); }
        catch (...) { run.problems.push_back (what + " [" + run.preset + "]: unknown exception"); }
    }
    else
        run.problems.push_back (what + " [" + run.preset + "]: CRASH, signal " + std::to_string (sig));
    g_guard = 0;
    sigaction (SIGSEGV, &oldSegv, nullptr);
    sigaction (SIGBUS, &oldBus, nullptr);
    sigaction (SIGABRT, &oldAbrt, nullptr);
    sigaction (SIGFPE, &oldFpe, nullptr);
    sigaction (SIGILL, &oldIll, nullptr);
}

void fxAdd (Rig<FxUi>& r, int type)
{
    r.tap (r.ui.screen().chainBar.plusCard);
    r.ui.list().state.current = type - 1;
    r.ui.list().showCurrent();
    const ui::Rect row = r.ui.list().rowRect (type - 1);
    r.tapAt (row.x + row.w / 2, row.y + row.h / 2);
}

/** The choice number of a modulation source by its key ("env1", "lfo2", "macro1"). */
int sourceChoice (const char* key)
{
    const auto* t = synthyboy::modSourceTable();
    for (size_t i = 0; i < static_cast<size_t> (synthyboy::kNumModSources); ++i)
        if (std::strcmp (t[i].key, key) == 0)
            return static_cast<int> (i) + 1;
    return 1;
}

/** The first knob of the tree (the control a long press on opens the modulations page of). */
ui::Component* firstKnob (ui::Component* c)
{
    ui::Component* leaves[256];
    const int n = c->collectLeaves (leaves, 0, 256);
    for (int i = 0; i < n; ++i)
        if (dynamic_cast<ui::Knob*> (leaves[i]) && ! leaves[i]->rect().empty())
            return leaves[i];
    return nullptr;
}

/** The modulations page of a control: a long press on its first knob, as the gallery does; the page is opened directly when the press did not open it. */
template <class Ui>
void openMods (Rig<Ui>& r, ui::Component& full, int modsPage)
{
    r.refresh();
    if (ui::Component* k = firstKnob (&full))
        r.tapAt (k->rect().x + k->rect().w / 2, k->rect().y + k->rect().h / 2, 700);
    r.refresh();
    if (r.stack.top() != modsPage)
        r.stack.openDetail (modsPage);
    r.refresh();
}

std::string modKey (int i)
{
    char k[24];
    std::snprintf (k, sizeof k, "mod%02d.source", i);
    return k;
}

/** Every page of the layout in its states. */
void lintView (Runner& run, const layout::View& v, const LintInst& base)
{
    const std::string pg = v.name;
    auto sub = [&] (const char* nm, const std::function<void()>& fn) { guarded (run, pg + "/" + nm, fn); };

    switch (v.kind)
    {
        case layout::kKindPlay:
        {
            sub ("main", [&] {
                LintInst inst = base;
                PlayView pv (v.index, 0.0);
                UiStack stack;
                ViewInput in;
                in.instance = &inst; in.io = &inst; in.ui = &stack;
                in.live = inst.haveLive ? &inst.liveValues : nullptr;
                in.nowMs = 1000.0; pv.update (in);
                in.nowMs = 1500.0; pv.update (in);
                lintPage (run, pg, "main", [&] (gpu::SceneBuilder& sb) { pv.describe (sb, 0, 1280, 800); }, {}, true);
            });
            break;
        }
        case layout::kKindMacros:
        {
            for (int bank = 0; bank < 2; ++bank)
                sub (bank ? "bank2" : "bank1", [&] {
                    Rig<MacroUi> r (base, v.index);
                    r.ui.setBank (bank);
                    r.refresh();
                    lintRig (run, r, pg, bank ? "bank2" : "bank1", { &r.ui.view() });
                });
            sub ("targets", [&] {
                LintInst inst = base;
                inst.mod (1, sourceChoice ("macro1"), "filter1.cutoff", 45.0f);
                inst.mod (2, sourceChoice ("macro1"), "osc1.frame", 30.0f);
                Rig<MacroUi> r (inst, v.index);
                r.refresh();
                r.stack.openDetail (MacroUi::kPageTargets);
                r.refresh();
                lintRig (run, r, pg, "targets", { &r.ui.targetsPage() });
            });
            break;
        }
        case layout::kKindOscUi:
        {
            sub ("main", [&] { Rig<OscPageUi> r (base, v.index); r.refresh(); lintRig (run, r, pg, "main", { &r.ui.ui().full() }); });
            struct D { const char* n; int page; };
            const D ds[] = { { "unison", OscUi::kPageUnison }, { "grain", OscUi::kPageGrain }, { "envelope", OscUi::kPageEnvelope }, { "mods", OscUi::kPageMods },
                                { "wt-map", 16 }, { "more", 17 }, { "sample-region", 18 }, { "sample-slice", 19 }, { "grain-engine", 20 }, { "spec-filter", 21 }, { "spec-scan", 22 } };
            for (const D& d : ds)
            {
                if (v.index == 1 || v.index == 5)   // Sub and Noise have no tuning / unison / grain / envelope detail
                    if (d.page == OscUi::kPageUnison || d.page == OscUi::kPageGrain || d.page == OscUi::kPageEnvelope || d.page >= 16)
                        continue;
                sub (d.n, [&] {
                    LintInst inst = base;
                    if (d.page == OscUi::kPageMods)
                        inst.mod (1, sourceChoice ("lfo1"), "filter1.cutoff", 45.0f);
                    Rig<OscPageUi> r (inst, v.index);
                    if (d.page == OscUi::kPageMods)
                        openMods (r, r.ui.ui().full().top.pos, d.page);
                    else
                    {
                        r.refresh();
                        r.stack.openDetail (d.page);
                        r.refresh();
                    }
                    ui::Component* top = d.page == OscUi::kPageMods ? static_cast<ui::Component*> (&r.ui.ui().modsPage()) : (d.page >= 16 ? static_cast<ui::Component*> (&r.ui.ui().specPage()) : static_cast<ui::Component*> (&r.ui.ui().detail()));
                    lintRig (run, r, pg, d.n, { top });
                });
            }
            break;
        }
        case layout::kKindMixer:
            sub ("main", [&] { Rig<MixerUi> r (base, v.index); r.refresh(); lintRig (run, r, pg, "main", { &r.ui.screen() }); });
            break;
        case layout::kKindFilterUi:
        {
            // ONE Filter tab: the four states F1 | F2 | NOTE | VELO (the chunk-only view state of the instance), the Formant and Comb models, the F1+F2 view, the pages above it
            auto filterRig = [&] (LintInst inst, int state, int model) -> std::unique_ptr<Rig<FilterUi>>
            {
                inst.tabViewState[IInstanceView::kTabViewFilter] = state;
                if (model >= 0)
                    inst.set (state == 1 ? "filter2.model" : "filter1.model", static_cast<float> (model));
                auto r = std::make_unique<Rig<FilterUi>> (inst, 1);
                int idx[2];
                synthyboy::FilterResponse::readoutIndices (state == 1 ? 2 : 1, idx);
                static float live[4];
                live[0] = live[1] = live[2] = live[3] = 0.0f;
                live[state == 1 ? 2 : 0] = r->inst.param (idx[0]);
                live[state == 1 ? 3 : 1] = r->inst.param (idx[1]);
                r->in.targets = live;
                r->refresh();
                return r;
            };
            sub ("main", [&] { auto r = filterRig (base, 0, -1); lintRig (run, *r, pg, "main", { &r->ui.full(), &r->ui.curveUi().screen }); });
            sub ("f2-velo", [&] { auto r = filterRig (base, 5, -1); lintRig (run, *r, pg, "f2-velo", { &r->ui.full(), &r->ui.curveUi().screen }); });
            sub ("formant", [&] { auto r = filterRig (base, 0, 5); lintRig (run, *r, pg, "formant", { &r->ui.full(), &r->ui.curveUi().screen }); });
            sub ("comb", [&] { auto r = filterRig (base, 0, 6); lintRig (run, *r, pg, "comb", { &r->ui.full(), &r->ui.curveUi().screen }); });
            sub ("expanded-main", [&] { auto r = filterRig (base, 6, -1); lintRig (run, *r, pg, "expanded-main", { &r->ui.full() }); });
            sub ("expanded-formant", [&] { auto r = filterRig (base, 6, 5); lintRig (run, *r, pg, "expanded-formant", { &r->ui.full() }); });
            sub ("expanded-comb", [&] { auto r = filterRig (base, 6, 6); lintRig (run, *r, pg, "expanded-comb", { &r->ui.full() }); });
            sub ("dual", [&] { auto r = filterRig (base, 6, -1); r->tap (r->ui.full().both); lintRig (run, *r, pg, "dual", { &r->ui.full() }); });
            for (int state = 2; state <= 3; ++state)
            {
                const std::string nm = state == 2 ? "note" : "velo";
                sub (nm.c_str(), [&] { auto r = filterRig (base, state, -1); lintRig (run, *r, pg, nm.c_str(), { &r->ui.full(), &r->ui.curveUi().screen }); });
                sub ((nm + "-curve").c_str(), [&] {   // a custom curve, LEGATO on, the glide chip, USED BY with rows
                    LintInst inst = base;
                    auto c = std::make_shared<synthyboy::StateContent>();
                    (state == 2 ? c->noteCurve : c->velocityCurve) = { { 0.0f, 0.1f, 2.0f }, { 0.3f, 0.7f, -3.0f }, { 0.7f, 0.2f, 0.0f }, { 1.0f, 1.0f, 0.0f } };
                    inst.setContent (c);
                    inst.set (state == 2 ? "curve.note.legato" : "curve.velocity.legato", 1.0f);
                    for (int i = 1; i <= 6; ++i)
                        inst.mod (i, state == 2 ? 24 : 23, i % 2 ? "filter1.cutoff" : "master.volume", 10.0f * i, i % 3 == 0);
                    auto r = filterRig (inst, state, -1);
                    lintRig (run, *r, pg, (nm + "-curve").c_str(), { &r->ui.full(), &r->ui.curveUi().screen });
                });
                sub ((nm + "-targets").c_str(), [&] {
                    LintInst inst = base;
                    for (int i = 1; i <= 5; ++i)
                        inst.mod (i, state == 2 ? 24 : 23, i % 2 ? "filter1.cutoff" : "master.volume", 10.0f * i);
                    auto r = filterRig (inst, state, -1);
                    r->stack.openDetail (FilterUi::kPageTargets);
                    r->refresh();
                    lintRig (run, *r, pg, (nm + "-targets").c_str(), { &r->ui.targetsPage() });
                });
            }
            sub ("mods", [&] {
                auto r = filterRig (base, 0, -1);
                openMods (*r, r->ui.full(), FilterUi::kPageMods);
                lintRig (run, *r, pg, "mods", { &r->ui.modsPage() });
            });
            break;
        }
        case layout::kKindEnvUi:
        {
            // ONE Env tab since the merge: every one of the six envelopes is a state of it (Play Env = the selection)
            for (int e = 1; e <= 6; ++e)
            {
                const std::string sfx = e == 1 ? std::string() : "-env" + std::to_string (e);
                LintInst eb = base;
                eb.playSel[1] = e;
                const int src = sourceChoice (("env" + std::to_string (e)).c_str());
                sub (("no-targets" + sfx).c_str(), [&] {
                    LintInst inst = eb;
                    for (int i = 1; i <= 64; ++i)
                        if (static_cast<int> (inst.get (modKey (i))) == e)
                            inst.set (modKey (i), 0.0f);
                    Rig<EnvUi> r (inst, v.index); r.refresh(); lintRig (run, r, pg, ("no-targets" + sfx).c_str(), { &r.ui.full() });
                });
                sub (("targets" + sfx).c_str(), [&] {
                    LintInst inst = eb;
                    inst.mod (1, src, "filter1.cutoff", 45.0f);
                    Rig<EnvUi> r (inst, v.index); r.refresh(); lintRig (run, r, pg, ("main" + sfx).c_str(), { &r.ui.full() });
                });
                if (e > 1 && e < 6)
                    continue;   // the pages above the full screen are the same for every envelope: ENV 1 and ENV 6 (the first and the last selector cell)
                sub (("targets-page" + sfx).c_str(), [&] {
                    LintInst inst = eb;
                    inst.mod (1, src, "filter1.cutoff", 45.0f);
                    Rig<EnvUi> r (inst, v.index); r.refresh(); r.stack.openDetail (EnvUi::kPageTargets); r.refresh();
                    lintRig (run, r, pg, ("targets-page" + sfx).c_str(), { &r.ui.targetsPage() });
                });
                sub (("mods" + sfx).c_str(), [&] {
                    LintInst inst = eb;
                    inst.mod (1, src, "filter1.cutoff", 45.0f);
                    Rig<EnvUi> r (inst, v.index);
                    openMods (r, r.ui.full(), EnvUi::kPageMods);
                    lintRig (run, r, pg, ("mods" + sfx).c_str(), { &r.ui.modsPage() });
                });
            }
            // the selector: all six used (the widest row), and the beats variant
            sub ("all-used", [&] {
                LintInst inst = base;
                for (int e = 1; e <= 6; ++e)
                    inst.mod (e, sourceChoice (("env" + std::to_string (e)).c_str()), "filter1.cutoff", 20.0f);
                Rig<EnvUi> r (inst, v.index); r.refresh(); lintRig (run, r, pg, "all-used", { &r.ui.full() });
            });
            sub ("beats", [&] {
                LintInst inst = base;
                inst.set ("env1.units", 1.0f);
                Rig<EnvUi> r (inst, v.index); r.refresh(); lintRig (run, r, pg, "beats", { &r.ui.full() });
            });
            break;
        }
        case layout::kKindLfoUi:
        {
            // One tab page per source (the sub-tabs LFO .. LFO 10, RND 1 .. RND 4): the page of the view shows its own source (the view's index 1 .. 14), there is no selector
            const bool rnd = v.index > 10;
            const int n = rnd ? v.index - 10 : v.index;
            const std::string ownKey = (rnd ? "rlfo" : "lfo") + std::to_string (n);
            struct M { const char* n; float sync; };
            const M ms[] = { { "hz", 0.0f }, { "bpm", 1.0f }, { "key", 4.0f } };
            for (const M& m : ms)
                sub (m.n, [&] {
                    LintInst inst = base;
                    inst.set (ownKey + ".sync", m.sync);
                    Rig<LfoUi> r (inst, v.index); r.refresh(); lintRig (run, r, pg, m.n, { &r.ui.full() });
                });
            if (! rnd)
            {
                sub ("drawn", [&] {   // EDIT is there while the drawn shape is shown
                    LintInst inst = base;
                    inst.set (ownKey + ".shape", 12.0f);
                    Rig<LfoUi> r (inst, v.index); r.refresh(); lintRig (run, r, pg, "drawn", { &r.ui.full() });
                });
                sub ("drawn-bpm", [&] {
                    LintInst inst = base;
                    inst.set (ownKey + ".shape", 12.0f);
                    inst.set (ownKey + ".sync", 1.0f);
                    Rig<LfoUi> r (inst, v.index); r.refresh(); lintRig (run, r, pg, "drawn-bpm", { &r.ui.full() });
                });
            }
            sub ("base", [&] { Rig<LfoUi> r (base, v.index); r.refresh(); lintRig (run, r, pg, "base", { &r.ui.full() }); });
            if (! rnd)
            {
                sub ("editor", [&] { Rig<LfoUi> r (base, v.index); r.refresh(); r.stack.openDetail (LfoUi::kPageEditor); r.refresh(); lintRig (run, r, pg, "editor", { &r.ui.editor() }); });
                // the editor in its other states: the LOOP marker (a loop mode) with a selection of every point, the page of the modulation of a point, the lists of SHAPES and MORE over the editor
                sub ("editor-loop", [&] {
                    LintInst inst = base;
                    inst.set ("lfo" + std::to_string (v.index) + ".sync_type", 4.0f);
                    Rig<LfoUi> r (inst, v.index); r.refresh(); r.stack.openDetail (LfoUi::kPageEditor); r.refresh();
                    r.ui.editor().canvas.selectAll(); r.refresh();
                    lintRig (run, r, pg, "editor-loop", { &r.ui.editor() });
                });
                sub ("editor-pointmod", [&] {
                    Rig<LfoUi> r (base, v.index); r.refresh(); r.stack.openDetail (LfoUi::kPageEditor); r.refresh();
                    r.ui.editor().canvas.selectOnly (1); r.refresh();
                    r.stack.openDetail (LfoUi::kPagePointMod); r.refresh();
                    lintRig (run, r, pg, "editor-pointmod", { &r.ui.pointModPage() });
                });
                sub ("editor-shapes-list", [&] {
                    Rig<LfoUi> r (base, v.index); r.refresh(); r.stack.openDetail (LfoUi::kPageEditor); r.refresh();
                    r.tap (r.ui.editor().shapes); r.refresh();
                    lintRig (run, r, pg, "editor-shapes-list", { &r.ui.list() }, true);
                });
                sub ("editor-more-list", [&] {
                    Rig<LfoUi> r (base, v.index); r.refresh(); r.stack.openDetail (LfoUi::kPageEditor); r.refresh();
                    r.tap (r.ui.editor().more); r.refresh();
                    lintRig (run, r, pg, "editor-more-list", { &r.ui.list() }, true);
                });
            }
            sub ("targets-page", [&] {
                LintInst inst = base;
                inst.mod (1, sourceChoice (ownKey.c_str()), "filter1.cutoff", 45.0f);
                Rig<LfoUi> r (inst, v.index); r.refresh(); r.stack.openDetail (LfoUi::kPageTargets); r.refresh(); lintRig (run, r, pg, "targets-page", { &r.ui.targetsPage() });
            });
            sub ("mods", [&] {
                LintInst inst = base;
                inst.mod (1, sourceChoice (ownKey.c_str()), "filter1.cutoff", 45.0f);
                Rig<LfoUi> r (inst, v.index);
                openMods (r, r.ui.full(), LfoUi::kPageMods);
                lintRig (run, r, pg, "mods", { &r.ui.modsPage() });
            });
            break;
        }
        case layout::kKindMod:
        {
            sub ("empty", [&] {
                LintInst inst = base;
                for (int i = 1; i <= 64; ++i)
                    inst.set (modKey (i), 0.0f);
                Rig<ModUi> r (inst, v.index); r.refresh(); lintRig (run, r, pg, "empty", { &r.ui.matrix() });
            });
            LintInst rowsInst = base;
            bool has = false;
            for (int i = 1; i <= 64 && ! has; ++i)
                has = rowsInst.get (modKey (i)) > 0.0f;
            if (! has)
            {
                rowsInst.mod (1, 7, "filter1.cutoff", 65.0f, true);
                rowsInst.mod (2, 1, "osc1.tune", -30.0f, true);
                rowsInst.mod (3, 19, "osc1.frame", 85.0f);
                rowsInst.mod (4, 27, "lfo1.rate", 50.0f);
                rowsInst.mod (5, 23, "master.volume", 40.0f);
            }
            sub ("rows", [&] { Rig<ModUi> r (rowsInst, v.index); r.refresh(); lintRig (run, r, pg, "rows", { &r.ui.matrix() }); });
            sub ("detail", [&] { Rig<ModUi> r (rowsInst, v.index); r.refresh(); r.stack.openDetail (ModUi::kPageDetail); r.refresh(); lintRig (run, r, pg, "detail", { &r.ui.detail() }); });
            sub ("targets-page", [&] { Rig<ModUi> r (rowsInst, v.index); r.refresh(); r.stack.openDetail (ModUi::kPageTargets); r.refresh(); lintRig (run, r, pg, "targets-page", { &r.ui.targetsPage() }); });
            break;
        }
        case layout::kKindFx:
        {
            sub ("gallery", [&] {
                LintInst inst = base;
                for (int i = 1; i <= 16; ++i)
                {
                    char k[24]; std::snprintf (k, sizeof k, "fxi%02d.type", i); inst.set (k, 0.0f);
                }
                for (const char* pfx : { "chorus", "compressor", "delay", "distortion", "eq", "fxfilter", "flanger", "phaser", "reverb", "hyper_dimension", "utility", "bode", "convolve" })
                    inst.set (std::string (pfx) + ".on", 0.0f);
                Rig<FxUi> r (inst, v.index); r.refresh(); lintRig (run, r, pg, "gallery", { &r.ui.screen() });
            });
            static const char* const kNames[14] = { "", "chorus", "compressor", "delay", "distortion", "eq", "filter", "flanger", "phaser", "reverb", "hyperdimension", "utility", "bode", "convolve" };
            for (int t = 1; t <= 13; ++t)
                sub (kNames[t], [&] {
                    Rig<FxUi> r (base, v.index);
                    r.refresh();
                    fxAdd (r, t);
                    const int n = r.ui.screen().slotEditor.numPages();
                    for (int p = 0; p < n; ++p)
                    {
                        r.ui.screen().slotEditor.setPage (p);
                        r.refresh();
                        lintRig (run, r, pg, std::string (kNames[t]) + "-p" + std::to_string (p + 1), { &r.ui.screen() });
                    }
                });
            // the three splitters: the page with its lanes (two effects in every band), the page of every band with its first effect, and the empty band (the gallery of the free types)
            static const char* const kSplit[3] = { "splitter-lh", "splitter-lmh", "splitter-ms" };
            for (int si = 0; si < 3; ++si)
                sub (kSplit[si], [&] {
                    const int kind = ui::fxl::kKindSplitterLh + si;
                    LintInst inst = base;
                    for (int t = 1; t < synthyboy::kNumFxInstanceTypes; ++t)   // a clean chain: every module off and in Main, the pool empty
                    {
                        inst.set (std::string (synthyboy::fxTypeDescriptor (t).prefix) + ".on", 0.0f);
                        inst.set (std::string (synthyboy::fxTypeDescriptor (t).prefix) + ".rack", 0.0f);
                    }
                    for (int i = 1; i <= 16; ++i)
                    {
                        char k[24]; std::snprintf (k, sizeof k, "fxi%02d.type", i); inst.set (k, 0.0f);
                    }
                    int next = 1;
                    for (int b = 0; b < ui::fxl::bandsOfSplitter (kind); ++b)
                        for (int n = 0; n < 2 + b; ++n, ++next)   // 2, 3, 4 effects in the bands
                        {
                            const std::string pre = synthyboy::fxTypeDescriptor (next).prefix;
                            inst.set (pre + ".on", 1.0f);
                            inst.set (pre + ".rack", static_cast<float> (ui::fxl::bandRack (kind, b)));
                        }
                    inst.set (std::string (si == 0 ? "splitter_lh" : (si == 1 ? "splitter_lmh" : "splitter_ms")) + ".on", 1.0f);
                    Rig<FxUi> r (inst, v.index);
                    r.refresh (2);
                    lintRig (run, r, pg, std::string (kSplit[si]) + "-lanes", { &r.ui.screen() });
                    for (int b = 0; b < ui::fxl::bandsOfSplitter (kind); ++b)
                    {
                        r.tap (r.ui.screen().slotEditor.bands.lane[b].tile);
                        lintRig (run, r, pg, std::string (kSplit[si]) + "-band" + std::to_string (b + 1), { &r.ui.screen() });
                        r.tap (r.ui.screen().chainBar.backCard);
                    }
                });
            break;
        }
        case layout::kKindMaster:
            sub ("main", [&] { Rig<MasterUi> r (base, v.index); r.ui.screen().setSide (ui::MasterScreen::SideMain); r.refresh(); lintRig (run, r, pg, "main", { &r.ui.screen() }); });
            sub ("tuning", [&] { Rig<MasterUi> r (base, 2); r.ui.screen().setSide (ui::MasterScreen::SideTuning); r.refresh(); lintRig (run, r, pg, "tuning", { &r.ui.screen() }); });
            // the conditional views of the bold cut: LINK off shows BEND DOWN, MPE on shows MPE BEND and BEND TO X (the presets have both in one state only)
            sub ("link_off", [&] { Rig<MasterUi> r (base, v.index); r.inst.set ("master.bend_down_link", 0.0f); r.ui.screen().setSide (ui::MasterScreen::SideMain); r.refresh(); lintRig (run, r, pg, "link_off", { &r.ui.screen() }); });
            sub ("link_on", [&] { Rig<MasterUi> r (base, v.index); r.inst.set ("master.bend_down_link", 1.0f); r.ui.screen().setSide (ui::MasterScreen::SideMain); r.refresh(); lintRig (run, r, pg, "link_on", { &r.ui.screen() }); });
            sub ("mpe_on", [&] { Rig<MasterUi> r (base, 2); r.inst.set ("master.mpe.on", 1.0f); r.ui.screen().setSide (ui::MasterScreen::SideTuning); r.refresh(); lintRig (run, r, pg, "mpe_on", { &r.ui.screen() }); });
            sub ("mpe_off", [&] { Rig<MasterUi> r (base, 2); r.inst.set ("master.mpe.on", 0.0f); r.ui.screen().setSide (ui::MasterScreen::SideTuning); r.refresh(); lintRig (run, r, pg, "mpe_off", { &r.ui.screen() }); });
            break;
        case layout::kKindVoice:
            sub ("main", [&] { Rig<VoiceUi> r (base, v.index); r.refresh(); lintRig (run, r, pg, "main", { &r.ui.screen() }); });
            break;
        default:
            run.problems.push_back (pg + ": the layout names a view of kind " + std::to_string (v.kind) + " that the lint has no driver for");
    }
}

void lintState (Runner& run, const std::string& presetName, const LintInst& base, const std::set<std::string>& only)
{
    run.sink.preset = presetName;
    run.preset = presetName;
    for (int i = 0; i < layout::kNumViews; ++i)
    {
        const layout::View& v = layout::kViews[i];
        if (! only.empty() && ! only.count (v.name))
            continue;
        lintView (run, v, base);
    }
}

// =============================================================================================================================================================
// self test: a synthetic page with one of each defect

class Box : public ui::Component
{
public:
    ui::Size min { 10, 10 };
    const char* text = nullptr;
    float textSize = 20.0f;
    ui::Size minSize() const override { return min; }
    void setBox (const ui::Rect& r) { rect_ = r; }

protected:
    void drawBody (ui::Pen& p) const override
    {
        p.control (rect_);
        if (text)
            p.text (static_cast<float> (rect_.x) + 2.0f, static_cast<float> (rect_.y) + 20.0f, textSize, text, ui::colour::rgba (0xffffff));
    }
};

class Page : public ui::Composite
{
public:
    Page() = default;
    void add (ui::Component& c) { addKid (c); }
    ui::Size minSize() const override { return { 10, 10 }; }
    void setBox (const ui::Rect& r) { rect_ = r; }
    bool titled = false;
    std::string title;
    ui::Rect blockRect;

protected:
    void layout() override {}
    void drawUnder (ui::Pen& p) const override
    {
        if (titled)
        {
            p.block (blockRect);
            p.text (static_cast<float> (blockRect.x) + 10.0f, static_cast<float> (blockRect.y) + 22.0f, 14.0f, title.c_str(), ui::colour::rgba (0xffffff));
        }
    }
};

int selfTest()
{
    int fails = 0;
    auto expect = [&] (bool ok, const char* what) { std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what); fails += ok ? 0 : 1; };
    auto has = [] (const Sink& s, const char* check, const char* needle = "") {
        for (const auto& kv : s.agg)
            if (kv.second.f.check == check && kv.second.f.detail.find (needle) != std::string::npos)
                return true;
        return false;
    };

    Page page;
    page.setBox ({ 0, 110, 1280, 628 });
    Box overA, overB, small, outside, wide, tA, tB, dupA, dupB, hugeCover;
    ui::Knob k1 ("A"), k2 ("B"), k3 ("C"), k4 ("D"), kChip ("E");
    Box lone;
    for (ui::Component* c : std::initializer_list<ui::Component*> { &overA, &overB, &small, &outside, &wide, &tA, &tB, &dupA, &dupB, &lone, &k1, &k2, &k3, &k4, &kChip })
        page.add (*c);
    overA.setBox ({ 20, 130, 100, 80 }); overB.setBox ({ 90, 150, 100, 80 });          // a: they overlap
    small.setBox ({ 300, 130, 40, 30 });                                                // b: 40 x 30
    outside.setBox ({ 1250, 700, 100, 80 });                                            // c: outside the region (and the parent)
    wide.setBox ({ 400, 130, 60, 60 }); wide.text = "A far too long text for this box"; // d: the text sticks out
    tA.setBox ({ 500, 130, 120, 60 }); tA.text = "OVERLAP"; tB.setBox ({ 500, 130, 120, 60 }); tB.text = "OVERLAP2";
    tA.textSize = 22.0f; tB.textSize = 22.0f;                                           // d: two texts on each other (and a: the same rectangle twice)
    dupA.setBox ({ 700, 130, 80, 60 }); dupB.setBox ({ 800, 130, 80, 60 }); dupA.setId (77); dupB.setId (77);   // f
    lone.setBox ({ 900, 130, 80, 60 });
    // h: four knob sizes
    k1.place ({ 20, 300, 100, 100 }); k2.place ({ 140, 300, 130, 130 }); k3.place ({ 300, 300, 160, 160 }); k4.place ({ 480, 300, 190, 190 });
    // e: a Q chip over the arc of a knob (the chip drawn on the top of the dial)
    kChip.place ({ 700, 300, 140, 140 });
    page.titled = true; page.title = "EMPTY BLOCK"; page.blockRect = { 20, 520, 400, 200 };   // g: a block with a title and nothing in it
    small.min = { 1, 1 };

    Capture cap;
    cap.page = "self";
    cap.state = "synthetic";
    cap.roots = { &page };
    auto scene = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder sb (*scene);
    sb.begin (gpu::kKindPlay, 0, 1280, 800);
    page.draw (sb);
    {
        ui::Pen pen (sb, true);
        // Seed the forbidden shape so the detector can still catch a regression.
        float vx, vy;
        kChip.valuePoint (vx, vy);
        const ui::Rect chip { static_cast<int>(vx) - 9, static_cast<int>(vy) - 8, 18, 17 };
        pen.rrect (chip, 5.0f, pen.c (ui::theme::L().q, 255));
        pen.textCentered (chip, "2", 13.0f, pen.c (ui::theme::L().onBright, 255), 8.0f);
    }
    parseScene (*scene, cap);
    Sink sink;
    analyse (cap, sink);
    expect (has (sink, "a", "overlap"), "a: overlapping controls found");
    expect (has (sink, "b", "40 x 30"), "b: a 40 x 30 touch rectangle found");
    expect (has (sink, "c", "drawing region"), "c: a control outside the drawing region found");
    expect (has (sink, "c", "parent"), "c: a control outside its parent found");
    expect (has (sink, "d", "sticks out"), "d: text wider than its control found");
    expect (has (sink, "d", "overlaps"), "d: two texts on each other found");
    expect (has (sink, "e", "Q chip"), "e: a Q chip over a knob found");
    expect (has (sink, "f", "share id 77"), "f: a duplicate id found");
    expect (has (sink, "g", "EMPTY BLOCK"), "g: a mostly empty titled block found");
    expect (has (sink, "h", "distinct knob diameters"), "h: more than three knob sizes found");

    // a clean page has no findings
    Page clean;
    clean.setBox ({ 0, 110, 1280, 628 });
    Box c1, c2;
    clean.add (c1); clean.add (c2);
    c1.setBox ({ 20, 130, 200, 100 }); c2.setBox ({ 240, 130, 200, 100 }); c1.text = "OK"; c2.text = "Fine";
    Capture cap2;
    cap2.page = "self";
    cap2.state = "clean";
    cap2.roots = { &clean };
    gpu::SceneBuilder sb2 (*scene);
    sb2.begin (gpu::kKindPlay, 0, 1280, 800);
    clean.draw (sb2);
    parseScene (*scene, cap2);
    Sink sink2;
    analyse (cap2, sink2);
    expect (sink2.agg.empty(), "a clean synthetic page has no findings");
    if (! sink2.agg.empty())
        for (const auto& kv : sink2.agg)
            std::printf ("      unexpected: %s: %s\n", kv.first.c_str(), kv.second.f.detail.c_str());
    std::printf (fails ? "layout_lint_host self-test: FAILED\n" : "layout_lint_host self-test: OK\n");
    return fails ? 1 : 0;
}

// =============================================================================================================================================================
// baseline, output

std::string jsonEsc (const std::string& s)
{
    std::string o;
    for (char c : s)
    {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (static_cast<unsigned char> (c) < 0x20) o += ' ';
        else o += c;
    }
    return o;
}

std::set<std::string> readBaseline (const std::string& path, bool& ok)
{
    std::set<std::string> keys;
    std::ifstream f (path, std::ios::binary);
    ok = f.is_open();
    if (! ok)
        return keys;
    std::string s ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
    const size_t arr = s.find ("\"findings\"");
    if (arr == std::string::npos)
        return keys;
    size_t i = s.find ('[', arr);
    if (i == std::string::npos)
        return keys;
    for (++i; i < s.size() && s[i] != ']'; ++i)
        if (s[i] == '"')
        {
            std::string k;
            for (++i; i < s.size() && s[i] != '"'; ++i)
            {
                if (s[i] == '\\' && i + 1 < s.size()) ++i;
                k += s[i];
            }
            keys.insert (k);
        }
    return keys;
}

void writeBaseline (const std::string& path, const std::map<std::string, Agg>& agg)
{
    std::ofstream o (path, std::ios::binary);
    o << "{\n  \"comment\": \"The findings of tools layout_lint_host that are known today (one key per line: page/state|check|control(s)). A new finding fails the test; a finding that is gone must be removed here (the program says which). Regenerate with --write-baseline.\",\n  \"findings\": [\n";
    bool first = true;
    for (const auto& kv : agg)
    {
        o << (first ? "    \"" : ",\n    \"") << jsonEsc (kv.first) << "\"";
        first = false;
    }
    o << "\n  ]\n}\n";
}

const char* sevName (int s) { return s == 1 ? "S1" : (s == 2 ? "S2" : (s == 3 ? "S3" : "S4")); }

} // namespace

int main (int argc, char** argv)
{
    std::string presetsDir, baselinePath, writeBaselinePath, jsonPath, reportPath;
    std::vector<std::string> presetFiles;
    std::set<std::string> only;
    bool noInit = false, noRender = false, selfTestMode = false, quiet = false, stats = false;

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&] () -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--presets-dir") presetsDir = next();
        else if (a == "--preset") presetFiles.push_back (next());
        else if (a == "--baseline") baselinePath = next();
        else if (a == "--write-baseline") writeBaselinePath = next();
        else if (a == "--json") jsonPath = next();
        else if (a == "--report") reportPath = next();
        else if (a == "--only") only.insert (next());
        else if (a == "--no-init") noInit = true;
        else if (a == "--no-render") noRender = true;
        else if (a == "--quiet") quiet = true;
        else if (a == "--stats") stats = true;
        else if (a == "--self-test") selfTestMode = true;
        else if (a == "--help" || a == "-h")
        {
            std::printf ("Usage: %s [--presets-dir DIR | --preset FILE ...] [--only PAGE ...] [--no-init] [--no-render] [--baseline FILE] [--write-baseline FILE] [--json FILE] [--report FILE] [--quiet] [--self-test]\n"
                         "Default presets: every .sbpreset under content/presets (found by walking the directory).\n", argv[0]);
            return 0;
        }
        else
        {
            std::fprintf (stderr, "unknown argument %s\n", a.c_str());
            return 2;
        }
    }
    if (selfTestMode)
        return selfTest();

    const auto t0 = std::chrono::steady_clock::now();
    const bool fullRun = presetFiles.empty() && only.empty() && ! noInit;
    if (presetFiles.empty())
    {
        if (presetsDir.empty())
            presetsDir = fs::exists ("content/presets") ? "content/presets" : std::string (LINT_SOURCE_DIR) + "/content/presets";
        if (fs::exists (presetsDir))
            for (const auto& e : fs::recursive_directory_iterator (presetsDir))
                if (e.is_regular_file() && e.path().extension() == ".sbpreset")
                    presetFiles.push_back (e.path().string());
        std::sort (presetFiles.begin(), presetFiles.end());
        if (presetFiles.empty())
        {
            std::fprintf (stderr, "no presets found under %s\n", presetsDir.c_str());
            return 2;
        }
    }

    Runner run;
    run.sink.stats = stats;
    run.render = ! noRender;
    std::vector<std::string> loadFailures;
    int presetsDone = 0;
    if (! noInit)
    {
        LintInst init;
        guarded (run, "Init", [&] { lintState (run, "Init", init, only); });
        ++presetsDone;
    }
    for (const std::string& p : presetFiles)
    {
        const std::string stem = fs::path (p).stem().string();
        LintInst inst;
        bool loaded = false;
        guarded (run, "load " + stem, [&] { loaded = inst.loadPresetFile (p); });
        if (! loaded)
        {
            loadFailures.push_back (p);
            continue;
        }
        guarded (run, stem, [&] { lintState (run, stem, inst, only); });
        ++presetsDone;
        run.render = false;   // the single colour check renders the pages of Init and of the first preset only: the CPU renderer is the slow part (about 7 s a pass)
    }
    for (const std::string& p : run.sink.problems)
        run.problems.push_back (p);
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();

    // ---- compare with the baseline
    std::set<std::string> base;
    bool haveBase = false;
    if (! baselinePath.empty())
        base = readBaseline (baselinePath, haveBase);
    if (! baselinePath.empty() && ! haveBase && writeBaselinePath.empty())
        std::fprintf (stderr, "baseline %s not found: every finding is new\n", baselinePath.c_str());

    std::vector<const Agg*> sorted, fresh;
    for (const auto& kv : run.sink.agg)
        sorted.push_back (&kv.second);
    std::sort (sorted.begin(), sorted.end(), [] (const Agg* a, const Agg* b) {
        if (a->f.severity != b->f.severity) return a->f.severity < b->f.severity;
        if (a->f.page != b->f.page) return a->f.page < b->f.page;
        if (a->f.state != b->f.state) return a->f.state < b->f.state;
        return a->f.key < b->f.key;
    });
    std::vector<std::string> stale;
    for (const std::string& k : base)
        if (! run.sink.agg.count (k))
            stale.push_back (k);
    for (const Agg* a : sorted)
        if (! base.count (a->f.key))
            fresh.push_back (a);

    // ---- the text report
    std::ostringstream out;
    std::map<std::string, int> perCheck;
    for (const Agg* a : sorted)
        ++perCheck[a->f.check];
    out << "layout_lint_host: " << run.pages << " page states from " << presetsDone << " instance states (Init + presets), " << run.ops << " scene ops, " << run.sink.raw << " raw findings, "
        << sorted.size() << " distinct findings, " << std::fixed;
    out.precision (1);
    out << secs << " s\n";
    out << "per check (distinct findings):";
    for (const char* c : { "a", "b", "c", "d", "e", "f", "g", "h" })
        out << " " << c << "=" << perCheck[c];
    out << "\n";
    std::string curPage;
    for (const Agg* a : sorted)
    {
        if (! quiet || ! base.count (a->f.key))
        {
            if (a->f.page != curPage)
            {
                curPage = a->f.page;
                out << "\n== page " << curPage << "\n";
            }
            out << "  " << sevName (a->f.severity) << " [" << a->f.check << "] " << a->f.state << (base.count (a->f.key) ? "" : "  NEW") << "  (" << a->presets.size() << " state" << (a->presets.size() == 1 ? "" : "s") << ", first: " << a->firstPreset << ")\n      " << a->f.detail << "\n";
        }
    }
    for (const std::string& f : loadFailures)
        out << "PRESET DID NOT LOAD: " << f << "\n";
    for (const std::string& p : run.problems)
        out << "PROBLEM: " << p << "\n";

    if (! writeBaselinePath.empty())
    {
        writeBaseline (writeBaselinePath, run.sink.agg);
        out << "baseline written: " << writeBaselinePath << " (" << run.sink.agg.size() << " findings)\n";
    }
    else if (! baselinePath.empty())
    {
        out << "\nnew findings (not in the baseline): " << fresh.size() << "\n";
        for (const Agg* a : fresh)
            out << "  NEW " << a->f.key << "\n";
        if (fullRun)
        {
            out << "baseline entries that no longer occur (remove them from " << baselinePath << "): " << stale.size() << "\n";
            for (const std::string& k : stale)
                out << "  GONE " << k << "\n";
        }
        else if (! stale.empty())
            out << "(" << stale.size() << " baseline entries not seen: not checked in a partial run)\n";
    }
    std::fputs (out.str().c_str(), stdout);
    if (! reportPath.empty())
    {
        std::ofstream r (reportPath, std::ios::binary);
        r << out.str();
    }

    if (! jsonPath.empty())
    {
        std::ofstream j (jsonPath, std::ios::binary);
        j << "{\n  \"pages\": " << run.pages << ", \"instanceStates\": " << presetsDone << ", \"seconds\": " << secs << ", \"rawFindings\": " << run.sink.raw << ",\n  \"presetLoadFailures\": [";
        for (size_t i = 0; i < loadFailures.size(); ++i)
            j << (i ? ", \"" : "\"") << jsonEsc (loadFailures[i]) << "\"";
        j << "],\n  \"problems\": [";
        for (size_t i = 0; i < run.problems.size(); ++i)
            j << (i ? ",\n    \"" : "\n    \"") << jsonEsc (run.problems[i]) << "\"";
        j << "],\n  \"findings\": [";
        bool first = true;
        for (const Agg* a : sorted)
        {
            j << (first ? "\n    " : ",\n    ") << "{\"page\": \"" << jsonEsc (a->f.page) << "\", \"state\": \"" << jsonEsc (a->f.state) << "\", \"check\": \"" << a->f.check << "\", \"severity\": " << a->f.severity
              << ", \"a\": \"" << jsonEsc (a->f.a) << "\", \"b\": \"" << jsonEsc (a->f.b) << "\", \"detail\": \"" << jsonEsc (a->f.detail) << "\", \"presets\": " << a->presets.size() << ", \"firstPreset\": \"" << jsonEsc (a->firstPreset)
              << "\", \"inBaseline\": " << (base.count (a->f.key) ? "true" : "false") << ", \"key\": \"" << jsonEsc (a->f.key) << "\"}";
            first = false;
        }
        j << "\n  ],\n  \"gone\": [";
        for (size_t i = 0; i < stale.size(); ++i)
            j << (i ? ", \"" : "\"") << jsonEsc (stale[i]) << "\"";
        j << "]\n}\n";
    }

    if (! writeBaselinePath.empty())
        return (loadFailures.empty() && run.problems.empty()) ? 0 : 1;
    if (baselinePath.empty())
        return (sorted.empty() && loadFailures.empty() && run.problems.empty()) ? 0 : 1;
    const bool ok = fresh.empty() && loadFailures.empty() && run.problems.empty() && (! fullRun || stale.empty());
    std::printf ("layout_lint_host: %s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
