// SynthyBoy - MPC UI component library: the two views of the ENVELOPE element (ENV 1 .. 6), layout presets built from library components (owner approved plan "ui-elem-envelope-lfo.md"; drawn for 1280 x 800,
// the Play page's tab is 1280 x 628 at y 110). Same contract as every component (core.h).
//  - EnvCompact: the third of the width (400 x 620) in the middle of the Play page: the instance selector (ENV 1 .. 6: the ONE selector of the three thirds, a Stepper with the list of its entries; the colour of the envelope and the dot "it has targets" stand in it), the envelope graph with a header band at its top (the "full screen" button at its right end: nothing of the graph lies under it; the handles
//    can be dragged here too), and under it the ADSR Q group (ATTACK, DECAY, SUSTAIN, RELEASE) in one frame.
//  - EnvFullScreen: the top bar (close, the instance selector, the targets button), the big graph (the whole width; the times stand under it and on the knobs, so there are no second boxes for them), the
//    list of what the envelope modulates at its right (only when it modulates something: otherwise the TARGETS button is enough), and the bottom band of three Q groups, each a titled block: ADSR [ATTACK, DECAY, SUSTAIN, RELEASE], SHAPE [A CURVE, D CURVE, R CURVE], TIME [DELAY, HOLD, BEATS, LEGATO].
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "native_apertures.h"

#include <array>

#include "carousel.h"
#include "env_graph.h"
#include "led_toggle.h"
#include "panel.h"
#include "play_rows.h"
#include "used_selector.h"

namespace synthyboy_mpc { namespace ui {

constexpr int kNumEnvs = 6;
/** The colour of envelope n (0 .. 5): the orange to red of the look, a step further along it for every envelope. */
inline uint32_t envColour (int n) noexcept
{
    const theme::Look& T = theme::L();
    return theme::mix (T.env[0], T.env[1], static_cast<float> (((n % kNumEnvs) + kNumEnvs) % kNumEnvs) / 5.0f);
}

// ===============================================================================================================================================================
/** EnvCarousel: the selector of the envelope in the compact view, the same component as the LFO's and the oscillator's (a Carousel: the current entry in the middle, the neighbours at its sides, a tap on a neighbour steps, a tap on the
    name opens the list of ENV 1 .. 6), with what the six tabs showed: a small dot in the colour of the envelope beside the name (and a smaller one beside the neighbour's) when the envelope has modulation targets (`setTargetMask`:
    bit n = envelope n + 1). The list rows carry the same dots (ListPopup::State::dotMask). */
class EnvCarousel : public Carousel
{
public:
    uint8_t targetMask_ = 0;
    void setTargetMask (uint8_t m) noexcept { targetMask_ = m; state.dotMask = m; state.dotColours = colours(); }
    uint8_t targetMask() const noexcept { return targetMask_; }
    bool hasTargets (int i) const noexcept { return i >= 0 && i < kNumEnvs && (targetMask_ & (1u << i)) != 0; }
    EnvCarousel()
    {
        static const char* const kNames[kNumEnvs] = { "ENV 1", "ENV 2", "ENV 3", "ENV 4", "ENV 5", "ENV 6" };
        state.count = kNumEnvs; state.wrap = true; state.names = kNames; state.dotColours = colours();
        setItem ("ENV 1");
    }
    /** The colours of the six envelopes (the list rows use them for their dots). */
    static const uint32_t* colours() noexcept
    {
        struct Table { uint32_t c[kNumEnvs]; Table() { for (int i = 0; i < kNumEnvs; ++i) c[i] = envColour (i); } };
        static const Table t;   // built once, thread safely (a table filled at every call was a data race between the threads that create the pages)
        return t.c;
    }
};

// ===============================================================================================================================================================
class EnvCompact : public Composite
{
public:
    EnvCarousel instances;
    EnvGraph graph;
    MenuButton full { "", MenuButton::Icon::Expand };
    Knob attack { "ATTACK" }, decay { "DECAY" }, sustain { "SUSTAIN" }, release { "RELEASE" };
    QGroup group;   // [ATTACK, DECAY, SUSTAIN, RELEASE]
    static constexpr int kKnobH = 84, kSelectorH = 56, kMargin = 6;

    EnvCompact()
    {
        for (Knob* k : { &attack, &decay, &sustain, &release })
        {
            k->state.side = true;
            k->state.dialMax = 96;   // the rows are as tall as the third leaves (about 108 px): the dial stays as big as before and the name and the value keep their room
        }
        addKid (instances); addKid (full); addKid (graph);   // the full screen button lies over the graph: it takes the touch first and is drawn last (draw below)
        addKid (attack); addKid (decay); addKid (sustain); addKid (release);
        group.set (0, &attack); group.set (1, &decay); group.set (2, &sustain); group.set (3, &release);
        group.setActive (true);
    }
    /** The Play skin owns this navigation row with a host-native selector. */
    void setNativeInstanceSelector (bool shown)
    {
        show (instances, shown);
        instances.setEnabled (shown);
    }
    void setNativeSustain() { show (sustain, false); }
    void setNativeDials()
    {
        show (attack, false);
        show (decay, false);
        show (sustain, false);
        show (release, false);
        attack.setEnabled (false);
        decay.setEnabled (false);
        sustain.setEnabled (false);
        release.setEnabled (false);
    }
    Size minSize() const override { return { 400, 560 }; }
    Rect graphRect() const noexcept { return graph.rect(); }

    void draw (gpu::SceneBuilder& sb) const override
    {
        {
            Pen pen (sb, enabled_, inactive_);
            Cell cell (sb, rect_);
            drawUnder (pen);
        }
        for (int i = 0; i < n_; ++i)
            if (shown_[i] && kids_[i] != &full)
                drawChild (sb, i);
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
        // the display is the one of the three thirds (play_rows.h); the knobs start at the common controls line (their Q chip lane lies above it) and share what is left of the third
        const Rect gr = playrows::displayRect (rect_);
        graph.place (gr);
        graph.plotArea = playrows::plotRect (gr);   // the curve and the handles lie below the header band; the full screen button stands in the band, the same place as the oscillator's and the LFO's
        full.place (playrows::fullRect (gr));
        const int by = playrows::controlsLine (rect_), kh = std::max (kKnobH, (rect_.bottom() - kMargin - by - kQLane) / 2);
        const int cw = (W - kGap) / 2;
        attack.place ({ rect_.x + kMargin, by, cw, kh });
        decay.place ({ rect_.x + kMargin + cw + kGap, by, W - cw - kGap, kh });
        sustain.place ({ rect_.x + kMargin, by + kh + kQLane, cw, kh });
        release.place ({ rect_.x + kMargin + cw + kGap, by + kh + kQLane, W - cw - kGap, kh });
    }
};

// ===============================================================================================================================================================
/** The names of the Q groups of the full screen (QGroup index 0 .. 2). */
class EnvFullScreen : public Composite
{
public:
    static constexpr int kMaxGroups = 3, kBottomH = 176, kTopH = 72;
    MenuButton close { "CLOSE", MenuButton::Icon::None };
    UsedSelector selector;                                         // ENV 1 .. 6 as "used + one free" (docs/ui-map/env-filter-lfo-plan.md (b)); the owner fills it every update
    MenuButton targets { "TARGETS", MenuButton::Icon::None };
    EnvGraph graph;
    Knob attack { "ATTACK" }, decay { "DECAY" }, sustain { "SUSTAIN" }, release { "RELEASE" };
    Knob aCurve { "A CURVE" }, dCurve { "D CURVE" }, rCurve { "R CURVE" };
    Knob delay { "DELAY" }, hold { "HOLD" };
    LedToggle beats, legato;                                       // MS / BEAT (on = beats), RETRIG (`legato_inverted`: on = the envelope restarts on a legato note): LED toggles in the cells of the knobs beside them
    QGroup groups[kMaxGroups];
    bool nativeADSR_ = false;
    bool nativeShape_ = false;
    bool nativeTime_ = false;
    bool nativeSelector_ = false;
    bool nativeTargets_ = false;

    /** Configure host-native ADSR dials: hides and disables overlay knobs and prevents overlay touch capture. */
    void setNativeADSR (bool native = true)
    {
        nativeADSR_ = native;
        show (attack, ! native);
        show (decay, ! native);
        show (sustain, ! native);
        show (release, ! native);
        attack.setEnabled (! native);
        decay.setEnabled (! native);
        sustain.setEnabled (! native);
        release.setEnabled (! native);
    }
    bool nativeADSR() const noexcept { return nativeADSR_; }

    /** Configure host-native SHAPE dials: hides and disables overlay curve knobs. */
    void setNativeShape (bool native = true)
    {
        nativeShape_ = native;
        show (aCurve, ! native);
        show (dCurve, ! native);
        show (rCurve, ! native);
        aCurve.setEnabled (! native);
        dCurve.setEnabled (! native);
        rCurve.setEnabled (! native);
    }
    bool nativeShape() const noexcept { return nativeShape_; }

    /** Configure host-native TIME controls: hides and disables delay, hold, beats, and legato. */
    void setNativeTime (bool native = true)
    {
        nativeTime_ = native;
        show (delay, ! native);
        show (hold, ! native);
        show (beats, ! native);
        show (legato, ! native);
        delay.setEnabled (! native);
        hold.setEnabled (! native);
        beats.setEnabled (! native);
        legato.setEnabled (! native);
    }
    bool nativeTime() const noexcept { return nativeTime_; }

    /** The skin draws and handles taps on the instance cells; the overlay retains wheel navigation. */
    void setNativeInstanceSelector (bool native = true)
    {
        nativeSelector_ = native && ! pageMode_;   // the page of a tab has no selector at all
        show (selector, ! native && ! pageMode_);
    }
    bool nativeInstanceSelector() const noexcept { return nativeSelector_; }

    /** The native TARGETS action opens the overlay’s target list. */
    void setNativeTargets (bool native = true)
    {
        nativeTargets_ = native;
        show (targets, ! native);
    }
    bool nativeTargets() const noexcept { return nativeTargets_; }

    /** Configure all remaining standalone controls (Shape, Time, Selector, Targets). */
    void setNativeRemaining (bool native = true)
    {
        setNativeShape (native);
        setNativeTime (native);
        setNativeInstanceSelector (native);
        setNativeTargets (native);
    }
    bool nativeRemaining() const noexcept
    {
        return nativeShape_ && nativeTime_ && nativeSelector_ && nativeTargets_;
    }

    /** Configure all standard controls natively. */
    void setNativeAll (bool native = true)
    {
        setNativeADSR (native);
        setNativeRemaining (native);
    }
    bool nativeAll() const noexcept { return nativeADSR_ && nativeRemaining(); }
    void setNativePlayEditor (bool active)
    {
        if (nativePlayEditor_ == active)
            return;
        nativePlayEditor_ = active;
        setNativeAll (active || pageMode_);
        close.setHostPainted (active);
        if (rect_.w > 0)
            layout();
    }


    /** Union bounding box of the ADSR controls. */
    Rect adsrArea() const noexcept
    {
        Rect r = attack.rect();
        r = Rect::unite (r, decay.rect());
        r = Rect::unite (r, sustain.rect());
        r = Rect::unite (r, release.rect());
        return r;
    }

    /** Union bounding box of the SHAPE controls. */
    Rect shapeArea() const noexcept
    {
        Rect r = aCurve.rect();
        r = Rect::unite (r, dCurve.rect());
        r = Rect::unite (r, rCurve.rect());
        return r;
    }

    /** Union bounding box of the TIME controls. */
    Rect timeArea() const noexcept
    {
        Rect r = delay.rect();
        r = Rect::unite (r, hold.rect());
        r = Rect::unite (r, beats.rect());
        r = Rect::unite (r, legato.rect());
        return r;
    }

    /** Bounding box of the instance selector. */
    Rect selectorArea() const noexcept
    {
        if (pageMode_)
            return {};   // no selector on the page of a tab
        return nativeSelector_ ? selector.rect() : Rect::unite (caption_, selector.rect());
    }

    /** Bounding box of the targets navigation button. */
    Rect targetsArea() const noexcept
    {
        return targets.rect();
    }

    std::array<Rect, 14> nativeApertures() const noexcept
    {
        const ControlModel* cells[] { &attack, &decay, &sustain, &release, &aCurve, &dCurve, &rCurve,
                                      &delay, &hold, &beats, &legato, &selector, &targets, &close };
        std::array<Rect, 14> areas {};
        for (size_t i = 0; i < areas.size(); ++i)
            if (isNativeComponent (cells[i]))
                areas[i] = cells[i]->rect();
        return areas;
    }

    bool isNativeADSR (int x, int y) const noexcept
    {
        return nativeADSR_ && (attack.rect().contains (x, y) || decay.rect().contains (x, y)
                            || sustain.rect().contains (x, y) || release.rect().contains (x, y));
    }

    bool isNativeShape (int x, int y) const noexcept
    {
        return nativeShape_ && (aCurve.rect().contains (x, y) || dCurve.rect().contains (x, y) || rCurve.rect().contains (x, y));
    }

    bool isNativeTime (int x, int y) const noexcept
    {
        return nativeTime_ && (delay.rect().contains (x, y) || hold.rect().contains (x, y)
                            || beats.rect().contains (x, y) || legato.rect().contains (x, y));
    }

    bool isNativeSelector (int x, int y) const noexcept
    {
        if (! nativeSelector_)
            return false;
        return selectorArea().contains (x, y);
    }

    bool isNativeTargets (int x, int y) const noexcept
    {
        if (! nativeTargets_)
            return false;
        return targets.rect().contains (x, y);
    }

    bool isNativeControl (int x, int y) const noexcept
    {
        return isNativeADSR (x, y) || isNativeShape (x, y) || isNativeTime (x, y) ||
               isNativeSelector (x, y) || isNativeTargets (x, y) || (nativePlayEditor_ && close.rect().contains (x, y));
    }

    bool isNativeComponent (const ControlModel* c) const noexcept
    {
        if (! c)
            return false;
        if (nativeADSR_ && (c == &attack || c == &decay || c == &sustain || c == &release))
            return true;
        if (nativeShape_ && (c == &aCurve || c == &dCurve || c == &rCurve))
            return true;
        if (nativeTime_ && (c == &delay || c == &hold || c == &beats || c == &legato))
            return true;
        if (nativeSelector_ && c == &selector)
            return true;
        if (nativeTargets_ && c == &targets)
            return true;
        return nativePlayEditor_ && c == &close;
    }

    /** What the envelope modulates, as tags in the top bar between the selector and the TARGETS button (the owner writes the lines: "name  +35%"; the first ones that fit are drawn, the others as "+n"); `numTargets` is the
        total. No side panel: the graph has the whole width with or without targets. */
    struct TargetLine { char text[40] = {}; };
    TargetLine targetLines[3];
    int numTargetLines = 0, numTargets = 0;
    void setNumTargets (int n) { numTargets = n; }
    /** The room of the tags in the top bar. */
    Rect tagsRect() const noexcept { return tags_; }

    EnvFullScreen()
    {
        beats.setLabel ("BEATS"); beats.centre = 0.36f;   // in the row of the knobs: the cell of a knob, the LED and the name at the height of the dials beside it
        legato.setLabel ("RETRIG"); legato.centre = 0.36f;   // owner safe list U11: the parameter is "legato inverted"; ON makes the envelope restart on every legato note (Voice::legatoTo), so the old label LEGATO said the opposite
        for (Knob* k : { &aCurve, &dCurve, &rCurve })
            k->state.bipolar = true;
        addKid (close); addKid (selector); addKid (targets); addKid (graph);
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
    /** The page of its own tab (the plugin draws the whole tab): there is nothing to close and no selector (the hardware sub-tab Env 1 .. Env 6 names the envelope: one tab page = one screen); the caption "ENV n" stands where
        the selector was and the tags of the targets take the width the selector had. */
    void setPageMode (bool page)
    {
        pageMode_ = page;
        show (close, ! page);
        setNativeADSR (page);
        setNativeShape (page);
        setNativeTime (page);
        setNativeInstanceSelector (false);
        show (selector, ! page);
        setNativeTargets (page);
        if (rect_.w > 0)
            layout();
    }
    /** The envelope the page names (1 .. 6) in its caption. */
    void setPageCaption (int env) noexcept
    {
        std::snprintf (caption_text_, sizeof caption_text_, "ENV %d", env);
    }
    /** The selector shows more or fewer cells than the last layout assumed (the owner filled it): the top bar is laid out again (the selector takes the width of its cells, the tags the rest). */
    void syncSelectorLayout()
    {
        if (rect_.w > 0 && selector.numCells() != laidCells_)
            layout();
    }
    /** The caption in front of the selector. */
    Rect captionRect() const noexcept { return caption_; }
    Rect block (int i) const noexcept { return i >= 0 && i < kMaxGroups ? blocks_[i] : Rect {}; }
    Size minSize() const override { return { 1280, 628 }; }

    Change touchDown (int x, int y, double nowMs) override
    {
        if (isNativeControl (x, y))
            return Change::none();
        const Change c = Composite::touchDown (x, y, nowMs);
        for (int i = 0; i < kMaxGroups; ++i)
        {
            if (i == 0 && nativeADSR_)
                continue;
            if (i == 1 && nativeShape_)
                continue;
            if (i == 2 && nativeTime_)
                continue;
            if (blocks_[i].contains (x, y) || groups[i].rect().contains (x, y))
            {
                setActiveGroup (i);
                break;
            }
        }
        return c;
    }

    void drawNativeGround (gpu::SceneBuilder& sb) const
    {
        drawNativeCanvas (sb, rect_, nativeApertures());
    }

    void drawFixedLabels (Pen& p) const
    {
        const theme::Look& T = theme::L();
        p.textLeft (caption_, pageMode_ && caption_text_[0] != '\0' ? caption_text_ : "ENV", pageMode_ ? theme::kPageTitleSize : T.fLabel + 2.0f, p.c (pageMode_ ? T.text : T.textDim, 255), T.fMin);   // the page title in page mode
        for (int i = 0; i < kMaxGroups; ++i)
        {
            p.textLeft ({ blocks_[i].x + KnobPanel::kBlockPad + 4, blocks_[i].y + 3, std::max (0, blocks_[i].w - 16), 16 }, groupName (i), T.fLabel - 2.0f, p.c (T.textDim, 255), T.fMin);
        }
    }

    void drawLiveLabels (Pen& p) const
    {
        const theme::Look& T = theme::L();
        // what the envelope modulates: tags in the top bar (text, no button outline: they are not touch targets; the TARGETS button opens the list), as many as fit, then "+n"
        const Rect t = tags_;
        if (t.w < 60 || numTargets <= 0)
            return;
        const float sz = T.fLabel;
        int x = t.x, drawn = 0;
        for (int i = 0; i < std::min (numTargetLines, numTargets); ++i)
        {
            char rest[12];
            std::snprintf (rest, sizeof rest, "+%d", numTargets - (i + 1));
            const int reserve = numTargets > i + 1 ? static_cast<int> (std::ceil (Pen::textWidth (rest, sz))) + 16 : 0;
            int w = static_cast<int> (std::ceil (Pen::textWidth (targetLines[i].text, sz))) + 22;
            if (x + w + reserve > t.right())
            {
                if (i > 0)
                    break;
                w = t.right() - x - reserve;   // the first one is cut with an ellipsis rather than left out
            }
            if (w < 60)
                break;
            p.disc (static_cast<float> (x + 5), static_cast<float> (t.y + t.h / 2), 3.0f, p.c (T.flow[0], 255));
            p.textLeft ({ x + 14, t.y, w - 14, t.h }, targetLines[i].text, sz, p.c (T.text, 255), T.fMin);
            x += w + 6;
            ++drawn;
        }
        if (drawn < numTargets)
        {
            char rest[12];
            std::snprintf (rest, sizeof rest, "+%d", numTargets - drawn);
            p.textLeft ({ x, t.y, std::max (0, t.right() - x), t.h }, rest, sz, p.c (T.textDim, 255), T.fMin);
        }
    }

    enum class Ground { Overlay, Apertures, Host };

    void drawPage (gpu::SceneBuilder& sb, Ground ground) const
    {
        if (ground == Ground::Overlay)
        {
            Pen pen (sb, enabled_, inactive_);
            Cell cell (sb, rect_);
            drawUnder (pen);
        }
        drawChildren (sb);
        {
            Pen p (sb, enabled_, inactive_);
            Cell cell (sb, rect_);
            if (ground != Ground::Host)
                drawFixedLabels (p);
            drawLiveLabels (p);
        }
        for (int i = 0; i < kMaxGroups; ++i)
        {
            if (i == 0 && nativeADSR_)
                continue;
            if (i == 1 && nativeShape_)
                continue;
            if (i == 2 && nativeTime_)
                continue;
            if (i == active_)
                groups[i].draw (sb);
            else
                groups[i].drawIdle (sb);
        }
    }

    void draw (gpu::SceneBuilder& sb) const override { drawPage (sb, Ground::Overlay); }
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
        close.place ({ nativePlayEditor_ ? rect_.x + 58 : r[0].x, top.y + (top.h - 56) / 2, r[0].w, 56 });
        laidCells_ = selector.numCells();
        caption_ = nativePlayEditor_ ? Rect {} : Rect { r[1].x, top.y + (top.h - 56) / 2, kCaptionW, 56 };
        const int captionW = pageMode_ ? kPageCaptionW : kCaptionW;
        caption_.w = nativePlayEditor_ ? 0 : captionW;
        const int selW = pageMode_ ? 0 : std::min (std::max (0, r[1].w - kCaptionW), std::max (1, laidCells_) * kSelectorCellW);
        selector.place ({ r[1].x + (pageMode_ ? captionW : kCaptionW), top.y + (top.h - 56) / 2, selW, 56 });
        targets.place ({ r[2].x, top.y + (top.h - 56) / 2, pageMode_ || nativePlayEditor_ ? 160 : r[2].w, 56 });
        const int selRight = selector.rect().right();
        tags_ = { selRight + 16, top.y + (top.h - 56) / 2, std::max (0, targets.rect().x - 12 - (selRight + 16)), 56 };
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
        auto inner = [&] (int b, int n, ControlModel** c, int cnt)
        {
            const Rect in { blocks_[b].x + pad, blocks_[b].y + kt, blocks_[b].w - 2 * pad, blocks_[b].h - kt - pad };
            const int cw = (in.w - kGap * (n - 1)) / n;
            for (int i = 0; i < cnt; ++i)
            {
                ControlModel* k = c[i];
                k->place ({ in.x + i * (cw + kGap), in.y, i == n - 1 ? in.w - i * (cw + kGap) : cw, in.h });
            }
        };
        ControlModel* adsr[4] = { &attack, &decay, &sustain, &release };
        ControlModel* shape[3] = { &aCurve, &dCurve, &rCurve };
        ControlModel* time[4] = { &delay, &hold, &beats, &legato };
        inner (0, 4, adsr, 4); inner (1, 3, shape, 3); inner (2, 4, time, 4);
        for (int i = 0; i < kMaxGroups; ++i)
            groups[i].setFrameRect (blocks_[i]);
        // ---- the middle: the graph (the whole width; a column of the targets at its right only when there are targets)
        const int midY = top.bottom() + m, midH = bandY - m - midY;
        graph.place ({ rect_.x + m, midY, W, std::max (0, midH) });
    }
    void drawUnder (Pen& p) const override
    {
        for (int i = 0; i < kMaxGroups; ++i)
        {
            if (i == 0 && nativeADSR_)
                continue;
            if (i == 1 && nativeShape_)
                continue;
            if (i == 2 && nativeTime_)
                continue;
            p.block (blocks_[i]);
        }
    }
    void drawExtra (Pen& p) const override
    {
        drawFixedLabels (p);
        drawLiveLabels (p);
    }
    static constexpr int kBlockGap = 8, kCaptionW = 56, kPageCaptionW = 96, kSelectorCellW = 88;
    char caption_text_[8] = {};
    Rect blocks_[kMaxGroups], tags_, caption_;
    int laidCells_ = 0;
    int active_ = -1;
    bool pageMode_ = false;
    bool nativePlayEditor_ = false;
};

}} // namespace synthyboy_mpc::ui
