// SynthyBoy - MPC UI component library: the two views of the ENVELOPE element (ENV 1 .. 6), layout presets built from library components (owner approved plan "ui-elem-envelope-lfo.md"; drawn for 1280 x 800,
// the Play page's tab is 1280 x 628 at y 110). Same contract as every component (core.h).
//  - EnvCompact: the third of the width (400 x 620) in the middle of the Play page: the instance selector (ENV 1 .. 6), the envelope graph with the "full screen" button in its top right corner (the handles
//    can be dragged here too), and under it the ADSR Q group (ATTACK, DECAY, SUSTAIN, RELEASE) in one frame.
//  - EnvFullScreen: the top bar (close, the instance selector, the targets button), the big graph at the left, the six exact fields (DELAY, ATTACK, HOLD, DECAY, SUSTAIN, RELEASE) and the list of what the
//    envelope modulates at the right, and the bottom band of three Q groups, each a titled block: ADSR [ATTACK, DECAY, SUSTAIN, RELEASE], SHAPE [A CURVE, D CURVE, R CURVE], TIME [DELAY, HOLD, BEATS, LEGATO].
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "env_graph.h"
#include "panel.h"

namespace synthyboy_mpc { namespace ui {

constexpr int kNumEnvs = 6;
/** The colour of envelope n (0 .. 5): the orange to red of the look, a step further along it for every envelope. */
inline uint32_t envColour (int n) noexcept
{
    const theme::Look& T = theme::L();
    return theme::mix (T.env[0], T.env[1], static_cast<float> (((n % kNumEnvs) + kNumEnvs) % kNumEnvs) / 5.0f);
}

// ===============================================================================================================================================================
/** EnvSelector: the six buttons ENV 1 .. 6, a PictogramSelector (the same touch rules: a tap on a cell, turn steps) that is drawn in the colours of the envelopes: the current one outlined in its own colour,
    the others carry a short bar of theirs. */
class EnvSelector : public PictogramSelector
{
public:
    EnvSelector()
    {
        for (int i = 0; i < kNumEnvs; ++i)
        {
            char t[8];
            std::snprintf (t, sizeof t, "ENV %d", i + 1);
            setItem (i, -1, t);
        }
    }

    /** The cells are as wide as the label "ENV n" at the size it is drawn in needs (at least 56). */
    Size minSize() const override
    {
        const int w = std::max (56, static_cast<int> (std::ceil (Pen::textWidth ("ENV 6", theme::L().fLabel + 2.0f))) + 14);
        return vertical() ? Size { w, std::max (1, count()) * kCellH } : Size { std::max (1, count()) * w, kCellH };
    }

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        for (int i = 0; i < count(); ++i)
        {
            const Rect c = cell (i).inset (2);
            const bool cur = i == state.index;
            const uint32_t col = envColour (i);
            p.control (c, false, cur);
            if (cur)
                p.rrect (c, T.rControl, p.c (col, 255), 1.8f);
            else
                p.rrect ({ c.x + c.w / 2 - 10, c.bottom() - 6, 20, 2 }, 1.0f, p.c (col, 200));
            p.textCentered ({ c.x + 2, c.y + 2, std::max (0, c.w - 4), std::max (0, c.h - 8) }, state.items[i].label, T.fLabel + 2.0f, p.c (cur ? T.text : T.textDim, 255), T.fMin);
        }
    }
};

// ===============================================================================================================================================================
class EnvCompact : public Composite
{
public:
    EnvSelector instances;
    EnvGraph graph;
    MenuButton full { "", MenuButton::Icon::Expand };
    Knob attack { "ATTACK" }, decay { "DECAY" }, sustain { "SUSTAIN" }, release { "RELEASE" };
    QGroup group;   // [ATTACK, DECAY, SUSTAIN, RELEASE]
    static constexpr int kKnobH = 84, kSelectorH = 56, kMargin = 6;

    EnvCompact()
    {
        for (Knob* k : { &attack, &decay, &sustain, &release })
            k->state.side = true;
        addKid (instances); addKid (full); addKid (graph);   // the full screen button lies over the graph: it takes the touch first and is drawn last (draw below)
        addKid (attack); addKid (decay); addKid (sustain); addKid (release);
        group.set (0, &attack); group.set (1, &decay); group.set (2, &sustain); group.set (3, &release);
        group.setActive (true);
    }
    Size minSize() const override { return { 400, 560 }; }
    Rect graphRect() const noexcept { return graph.rect(); }

    void draw (gpu::SceneBuilder& sb) const override
    {
        {
            Pen pen (sb, enabled_);
            Cell cell (sb, rect_);
            drawUnder (pen);
        }
        for (int i = 0; i < n_; ++i)
            if (shown_[i] && kids_[i] != &full)
                kids_[i]->draw (sb);
        full.draw (sb);
        group.draw (sb);
    }
    /** The running dots over the graph (not cached). */
    void drawLive (gpu::SceneBuilder& sb) const { graph.drawLive (sb); }

protected:
    void layout() override
    {
        const int W = rect_.w - 2 * kMargin;
        instances.place ({ rect_.x + kMargin, rect_.y + kMargin, W, kSelectorH });
        const int knobsH = kQLane + 2 * kKnobH + kQLane, by = rect_.bottom() - kMargin - knobsH + kQLane;
        const int gy = rect_.y + kMargin + kSelectorH + 8, gh = std::max (0, by - kQLane - 4 - gy);
        graph.place ({ rect_.x + kMargin, gy, W, gh });
        full.place ({ graph.rect().right() - 64 - 4, graph.rect().y + 4, 64, 56 });
        const int cw = (W - kGap) / 2;
        attack.place ({ rect_.x + kMargin, by, cw, kKnobH });
        decay.place ({ rect_.x + kMargin + cw + kGap, by, W - cw - kGap, kKnobH });
        sustain.place ({ rect_.x + kMargin, by + kKnobH + kQLane, cw, kKnobH });
        release.place ({ rect_.x + kMargin + cw + kGap, by + kKnobH + kQLane, W - cw - kGap, kKnobH });
    }
};

// ===============================================================================================================================================================
/** The names of the Q groups of the full screen (QGroup index 0 .. 2). */
class EnvFullScreen : public Composite
{
public:
    static constexpr int kMaxGroups = 3, kBottomH = 176, kTopH = 72, kFields = 6;
    MenuButton close { "CLOSE", MenuButton::Icon::None };
    EnvSelector instances;
    MenuButton targets { "TARGETS", MenuButton::Icon::None };
    EnvGraph graph;
    NumberField fields[kFields];                                   // DELAY, ATTACK, HOLD, DECAY, SUSTAIN, RELEASE
    Knob attack { "ATTACK" }, decay { "DECAY" }, sustain { "SUSTAIN" }, release { "RELEASE" };
    Knob aCurve { "A CURVE" }, dCurve { "D CURVE" }, rCurve { "R CURVE" };
    Knob delay { "DELAY" }, hold { "HOLD" };
    ToggleIcon beats, legato;                                      // MS / BEAT (on = beats), LEGATO
    QGroup groups[kMaxGroups];

    /** The what-it-modulates list at the right (the owner writes the lines: "name  +35%"); `numTargets` is the total. */
    struct TargetLine { char text[40] = {}; };
    TargetLine targetLines[3];
    int numTargetLines = 0, numTargets = 0;

    EnvFullScreen()
    {
        static const char* const kField[kFields] = { "DELAY", "ATTACK", "HOLD", "DECAY", "SUSTAIN", "RELEASE" };
        for (int i = 0; i < kFields; ++i)
        {
            fields[i].setLabel (kField[i]);
            fields[i].state.format = NumberField::Format::IntPlain;
        }
        beats.state.icon = ToggleIcon::Icon::Generic; beats.setLabel ("BEATS"); beats.state.showState = false;
        legato.state.icon = ToggleIcon::Icon::Generic; legato.setLabel ("LEGATO");
        for (Knob* k : { &aCurve, &dCurve, &rCurve })
            k->state.bipolar = true;
        addKid (close); addKid (instances); addKid (targets); addKid (graph);
        for (NumberField& f : fields) addKid (f);
        for (Knob* k : { &attack, &decay, &sustain, &release, &aCurve, &dCurve, &rCurve, &delay, &hold })
            addKid (*k);
        addKid (beats); addKid (legato);
        groups[0].set (0, &attack); groups[0].set (1, &decay); groups[0].set (2, &sustain); groups[0].set (3, &release);
        groups[1].set (0, &aCurve); groups[1].set (1, &dCurve); groups[1].set (2, &rCurve);
        groups[2].set (0, &delay); groups[2].set (1, &hold); groups[2].set (2, &beats); groups[2].set (3, &legato);
        setActiveGroup (0);
    }
    static const char* groupName (int i) noexcept { static const char* const n[kMaxGroups] = { "ADSR", "SHAPE", "TIME" }; return i >= 0 && i < kMaxGroups ? n[i] : ""; }
    int activeGroup() const noexcept { return active_; }
    void setActiveGroup (int i)
    {
        for (int k = 0; k < kMaxGroups; ++k)
            groups[k].setActive (k == i);
        active_ = i >= 0 && i < kMaxGroups ? i : -1;
    }
    Change turnQ (int knobIndex, double delta) { return active_ >= 0 ? groups[active_].turn (knobIndex, delta) : Change::none(); }
    /** The page of its own tab (the plugin draws the whole tab): there is nothing to close, the selector takes the place of CLOSE. */
    void setPageMode (bool page)
    {
        pageMode_ = page;
        show (close, ! page);
        if (rect_.w > 0)
            layout();
    }
    Rect block (int i) const noexcept { return i >= 0 && i < kMaxGroups ? blocks_[i] : Rect {}; }
    Rect targetsBlock() const noexcept { return targetsBlock_; }
    Size minSize() const override { return { 1280, 628 }; }

    Change touchDown (int x, int y, double nowMs) override
    {
        const Change c = Composite::touchDown (x, y, nowMs);
        if (cap_)
            for (int i = 0; i < kMaxGroups; ++i)
                if (groups[i].rect().contains (x, y))
                {
                    setActiveGroup (i);
                    break;
                }
        return c;
    }
    void draw (gpu::SceneBuilder& sb) const override
    {
        Composite::draw (sb);
        for (const QGroup& g : groups)
            g.draw (sb);
    }
    void drawLive (gpu::SceneBuilder& sb) const { graph.drawLive (sb); }

protected:
    void layout() override
    {
        const int m = 8, W = rect_.w - 2 * m;
        // ---- the top bar: close, the selector, the targets button
        const Rect top { rect_.x + m, rect_.y + m, W, kTopH - m };
        Rect r[3];
        const int fixed[3] = { pageMode_ ? 0 : 96, 0, 200 }, weight[3] = { 0, 1, 0 };
        layoutRow (top, fixed, weight, 3, kGap, r);
        close.place ({ r[0].x, top.y + (top.h - 56) / 2, r[0].w, 56 });
        const int selW = std::min (r[1].w, 6 * 120);
        instances.place ({ r[1].x, top.y + (top.h - 56) / 2, selW, 56 });
        targets.place ({ r[2].x, top.y + (top.h - 56) / 2, r[2].w, 56 });
        // ---- the bottom band: three blocks (a Q group each), in proportion to the members
        const int bandY = rect_.bottom() - m - kBottomH;
        const int unit = (W - 2 * kBlockGap) / 11;
        const int bw[3] = { 4 * unit, 3 * unit, W - 2 * kBlockGap - 7 * unit };
        int bx = rect_.x + m;
        for (int i = 0; i < 3; ++i)
        {
            blocks_[i] = { bx, bandY, bw[i], kBottomH };
            bx += bw[i] + kBlockGap;
        }
        const int kt = KnobPanel::kBlockTitleH, pad = KnobPanel::kBlockPad;
        auto inner = [&] (int b, int n, Component** c, int cnt)
        {
            const Rect in { blocks_[b].x + pad, blocks_[b].y + kt, blocks_[b].w - 2 * pad, blocks_[b].h - kt - pad };
            const int cw = (in.w - kGap * (n - 1)) / n;
            for (int i = 0; i < cnt; ++i)
            {
                Component* k = c[i];
                const int h = k == &beats || k == &legato ? std::min (in.h, 96) : in.h;
                k->place ({ in.x + i * (cw + kGap), in.y + (in.h - h) / 2, i == n - 1 ? in.w - i * (cw + kGap) : cw, h });
            }
        };
        Component* adsr[4] = { &attack, &decay, &sustain, &release };
        Component* shape[3] = { &aCurve, &dCurve, &rCurve };
        Component* time[4] = { &delay, &hold, &beats, &legato };
        inner (0, 4, adsr, 4); inner (1, 3, shape, 3); inner (2, 4, time, 4);
        // ---- the middle: the graph (about two thirds of the width) and the right column
        const int midY = top.bottom() + m, midH = bandY - m - midY;
        const int gw = (W - m) * 64 / 100;
        graph.place ({ rect_.x + m, midY, gw, std::max (0, midH) });
        const int rx = rect_.x + m + gw + m, rw = rect_.right() - m - rx;
        const int fw = (rw - kGap) / 2, fh = 56;
        for (int i = 0; i < kFields; ++i)
            fields[i].place ({ rx + (i % 2) * (fw + kGap), midY + (i / 2) * (fh + kGap), i % 2 == 0 ? fw : rw - fw - kGap, fh });
        const int ty = midY + 3 * (fh + kGap) + 4;
        targetsBlock_ = { rx, ty, rw, std::max (0, midY + midH - ty) };
    }
    void drawUnder (Pen& p) const override
    {
        for (int i = 0; i < kMaxGroups; ++i)
            p.block (blocks_[i]);
        p.block (targetsBlock_);
    }
    void drawExtra (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        for (int i = 0; i < kMaxGroups; ++i)
            p.textLeft ({ blocks_[i].x + KnobPanel::kBlockPad + 4, blocks_[i].y + 3, std::max (0, blocks_[i].w - 16), 16 }, groupName (i), T.fLabel - 2.0f, p.c (T.textDim, 255), T.fMin);
        // what the envelope modulates: the first lines of the list (the TARGETS button opens all of it)
        const Rect t = targetsBlock_;
        if (t.h < 40)
            return;
        char head[32];
        std::snprintf (head, sizeof head, numTargets == 1 ? "MODULATES %d TARGET" : "MODULATES %d TARGETS", numTargets);
        p.textLeft ({ t.x + 12, t.y + 4, std::max (0, t.w - 24), 24 }, head, T.fLabel - 2.0f, p.c (T.textDim, 255), T.fMin);
        if (numTargets == 0)
        {
            p.textLeft ({ t.x + 12, t.y + 32, std::max (0, t.w - 24), 28 }, "NOTHING YET", T.fValue - 4.0f, p.c (T.textFaint, 255), T.fMin);
            return;
        }
        for (int i = 0; i < numTargetLines; ++i)
        {
            const Rect line { t.x + 12, t.y + 30 + i * 34, std::max (0, t.w - 24), 30 };
            if (line.bottom() > t.bottom() - 2)
                break;
            p.textLeft (line, targetLines[i].text, T.fValue - 2.0f, p.c (T.text, 255), T.fMin);
        }
    }
    static constexpr int kBlockGap = 8;
    Rect blocks_[kMaxGroups], targetsBlock_;
    int active_ = -1;
    bool pageMode_ = false;
};

}} // namespace synthyboy_mpc::ui
