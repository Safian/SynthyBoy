// SynthyBoy - MPC UI component library: the COMPOSITE components, built only from the base components (base.h) and with the same contract (core.h): UnisonBox, WarpSlot,
// WarpPair, KnobRandPair. The ids of the children are given by the owner (`child.setId`); a change comes out with the id of the child it came from. A composite places its children
// inside its own rectangle (place), disables them with itself, and routes the touch of a gesture to the child it went down on until the touch up (also outside the rectangle).
// turn (delta) goes to the PRIMARY child of the composite (UnisonBox: the up / down stepper, WarpSlot and KnobRandPair: the knob), the others have none.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "base.h"
#include "qgroup.h"

namespace synthyboy_mpc { namespace ui {

/** Splits `r` into a row of n rectangles: an entry with fixed[i] > 0 has exactly that width, the others share the rest in proportion to weight[i]; `gap` pixels between them. */
inline void layoutRow (const Rect& r, const int* fixed, const int* weight, int n, int gap, Rect* out) noexcept
{
    int fixedSum = 0, weightSum = 0;
    for (int i = 0; i < n; ++i)
    {
        fixedSum += fixed[i] > 0 ? fixed[i] : 0;
        weightSum += fixed[i] > 0 ? 0 : weight[i];
    }
    const int free = std::max (0, r.w - fixedSum - gap * (n - 1));
    int x = r.x, usedWeight = 0;
    for (int i = 0; i < n; ++i)
    {
        int w;
        if (fixed[i] > 0)
            w = fixed[i];
        else
        {
            const int before = weightSum > 0 ? free * usedWeight / weightSum : 0;
            usedWeight += weight[i];
            w = (weightSum > 0 ? free * usedWeight / weightSum : 0) - before;
        }
        out[i] = { x, r.y, std::max (0, std::min (w, r.right() - x)), r.h };
        x += w + gap;
    }
}

// ===============================================================================================================================================================
class Composite : public Component
{
public:
    static constexpr int kMaxKids = 160;

    void place (const Rect& r) override { rect_ = r; layout(); }
    void setEnabled (bool e) override
    {
        if (enabled_ == e)
            return;
        enabled_ = e;
        if (! e)
            cap_ = nullptr;
        syncKids();
    }
    void setInactive (bool i) override { inactive_ = i; for (int k = 0; k < n_; ++k) kids_[k]->setInactive (i); }
    void setQ (bool frame, int index) override { qFrame_ = frame; qIndex_ = frame ? index : 0; for (int i = 0; i < n_; ++i) kids_[i]->setQ (frame, 0); }

    void draw (gpu::SceneBuilder& sb) const override
    {
        Pen pen (sb, enabled_, inactive_);
        {
            Cell cell (sb, rect_);
            drawUnder (pen);
        }
        drawChildren (sb);
        Cell cell (sb, rect_);
        drawExtra (pen);
    }
    Change touchDown (int x, int y, double nowMs) override
    {
        cap_ = nullptr;
        if (! enabled_ || ! rect_.contains (x, y))
            return Change::none();
        for (int i = 0; i < n_; ++i)
            if (shown_[i] && kids_[i]->rect().contains (x, y))
            {
                cap_ = kids_[i];
                return cap_->touchDown (x, y, nowMs);
            }
        return Change::none();
    }
    Change touchMove (int x, int y) override { return cap_ ? cap_->touchMove (x, y) : Change::none(); }
    Change touchMove (int x, int y, double nowMs) override { return cap_ ? cap_->touchMove (x, y, nowMs) : Change::none(); }
    Change touchUp (int x, int y) override
    {
        Component* k = cap_;
        cap_ = nullptr;
        return k ? k->touchUp (x, y) : Change::none();
    }
    Change tick (double nowMs) override { return cap_ ? cap_->tick (nowMs) : Change::none(); }
    Change turn (double delta) override { return enabled_ && primary_ ? primary_->turn (delta) : Change::none(); }
    bool wheelOver (int x, int y, int notches, Change& out) override
    {
        if (! enabled_ || ! rect_.contains (x, y))
            return false;
        for (int i = 0; i < n_; ++i)
            if (shown_[i] && kids_[i]->wheelOver (x, y, notches, out))
                return true;
        return false;
    }
    int touchTargets (Rect* out, int max) const override
    {
        int n = 0;
        for (int i = 0; i < n_; ++i)
            if (shown_[i])
                n += kids_[i]->touchTargets (out + n, max - n);
        return n;
    }
    int collectLeaves (Component** out, int n, int max) override
    {
        for (int i = 0; i < n_; ++i)
            if (shown_[i])
                n = kids_[i]->collectLeaves (out, n, max);
        return n;
    }
    /** The primary child (the one a hardware knob turns), null when the composite has none. */
    Component* primary() const noexcept { return primary_; }
    Component* primaryChild() const override { return primary_; }
    int numChildren() const noexcept { return n_; }
    Component* child (int i) const noexcept { return kids_[i]; }
    bool childShown (int i) const noexcept { return shown_[i]; }

protected:
    void drawChildren (gpu::SceneBuilder& sb) const
    {
        for (int i = 0; i < n_; ++i)
            if (shown_[i])
                kids_[i]->draw (sb);
    }

    virtual void layout() = 0;
    virtual void drawUnder (Pen&) const {}   // drawn before the children (backgrounds)
    virtual void drawExtra (Pen&) const {}   // drawn after the children (outlines, captions)
    virtual void syncKids() { for (int i = 0; i < n_; ++i) kids_[i]->setEnabled (enabled_); }
    void drawBody (Pen&) const override {}
    void addKid (Component& c, bool shown = true) { if (n_ < kMaxKids) { kids_[n_] = &c; shown_[n_] = shown; ++n_; } }
    void clearKids() { n_ = 0; cap_ = nullptr; primary_ = nullptr; }
    void show (const Component& c, bool s) { for (int i = 0; i < n_; ++i) if (kids_[i] == &c) shown_[i] = s; }
    void setPrimary (Component* c) { primary_ = c; }

    Component* kids_[kMaxKids] = {};
    bool shown_[kMaxKids] = {};
    int n_ = 0;
    Component* cap_ = nullptr;
    Component* primary_ = nullptr;
};

constexpr int kGap = 4;

// ===============================================================================================================================================================
/** UnisonBox: the voice count (the up / down stepper with the caption VOICES) and the settings button (the gear with the label MORE) under it. turn goes to the stepper. */
class UnisonBox : public Composite
{
public:
    DetailButton gear { DetailButton::Icon::Gear, "MORE" };
    UpDownStepper count;
    static constexpr int kGearH = 56;

    UnisonBox() { count.state.min = 1; count.state.max = 16; count.state.value = 1; count.setCaption ("VOICES"); addKid (gear); addKid (count); setPrimary (&count); }
    Size minSize() const override { return { 116, 112 + kGap + kGearH }; }

protected:
    void layout() override
    {
        gear.place ({ rect_.x, rect_.bottom() - kGearH, rect_.w, kGearH });
        count.place ({ rect_.x, rect_.y, rect_.w, std::max (0, rect_.h - kGap - kGearH) });
    }
};

// ===============================================================================================================================================================
/** 11. WarpSlot: the knob (the amount), the Stepper (the type; its text is the type name) and, when the owner says the current type has it, the extra value "WARP Var" (0 .. 1) as a
    small knob. With the type OFF the slot is drawn dim: the stepper's text dim, the knob and the extra value dim and without reaction (the stepper keeps working: it is how the type
    is chosen). `mirrored`: the knob on the right (the right hand slot of a WarpPair). turn goes to the amount knob. */
class WarpSlot : public Composite
{
public:
    Knob amount { "WARP" };
    Stepper type;
    Knob var { "VAR", false };

    explicit WarpSlot (bool mirrored = false) : mirrored_ (mirrored) { amount.state.side = true; var.state.small = true; var.state.side = true; var.state.dialMax = kVarDial; addKid (amount); addKid (type); addKid (var, false); setPrimary (&amount); }
    void setTypeOff (bool off) { typeOff_ = off; type.state.dim = off; syncKids(); }
    /** Hide what does not apply (bold cut): `false` hides the amount knob and the PHASE knob (a warp whose type is Off has no amount and no phase; the type stepper stays, it is how the type is chosen). The default is `true`: a slot that is
        not asked to hide anything draws both, dimmed with the type off (the Play page and the old pages). */
    void setApplies (bool a) { applies_ = a; show (amount, a); show (var, a && hasVar_); layout(); }
    bool applies() const noexcept { return applies_; }
    /** The slot has a whole block to itself (the single warp of the default view): the type stepper is up to 120 px tall instead of 64 (the block stays at least half covered, lint rule g). */
    void setTall (bool t) { tall_ = t; layout(); }
    void setHasVar (bool has) { hasVar_ = has; show (var, has && applies_); layout(); }
    void setMirrored (bool m) { mirrored_ = m; layout(); }
    bool typeOff() const noexcept { return typeOff_; }
    bool hasVar() const noexcept { return hasVar_; }
    bool mirrored() const noexcept { return mirrored_; }
    Size minSize() const override { return { kAmountW + kGap + Stepper::kMinW + (hasVar_ ? kGap + kVarW : 0), 72 }; }
    // the knob (the medium size: the dial at the left, the name and the value at its right), the name field and the VAR knob; the slot is centred in the room it gets
    static constexpr int kAmountW = 184, kStepperMaxW = 300, kVarW = 136, kVarDial = 52;   // the PHASE knob: a small dial with its name and value at the right, in the lines of the name and value of the amount knob

protected:
    void syncKids() override { amount.setEnabled (enabled_ && ! typeOff_); var.setEnabled (enabled_ && ! typeOff_); type.setEnabled (enabled_); }
    void layout() override
    {
        // order of the pieces from the left: knob, stepper, var; mirrored: var, stepper, knob. The stepper has a largest width and the VAR knob a small one: the slot is centred in its rectangle
        if (! applies_)   // hidden amount and PHASE: the type stepper has the room (a warp that is Off is only its type)
        {
            const int sw = rect_.w, sh = std::min (rect_.h, 120);   // (wide enough that its block stays at least half covered: lint rule g)
            amount.place ({}); var.place ({});
            type.place ({ rect_.x + (rect_.w - sw) / 2, rect_.y + (rect_.h - sh) / 2, sw, sh });
            syncKids();
            return;
        }
        const int contentW = kAmountW + kGap + kStepperMaxW + (hasVar_ ? kGap + kVarW : 0);
        const Rect area = rect_.w > contentW ? Rect { rect_.x + (rect_.w - contentW) / 2, rect_.y, contentW, rect_.h } : rect_;
        Rect r[3];
        const int fixed[3] = { kAmountW, 0, kVarW }, weight[3] = { 0, 1, 0 };
        const int n = hasVar_ ? 3 : 2;
        layoutRow (area, fixed, weight, n, kGap, r);
        Rect kr = r[0], sr = r[1], vr = hasVar_ ? r[2] : Rect {};
        if (mirrored_)
        {
            // the same pieces, from the right: build from the mirrored positions
            Rect m[3];
            const int mf[3] = { kVarW, 0, kAmountW }, mw[3] = { 0, 1, 0 };
            if (hasVar_)
            {
                layoutRow (area, mf, mw, 3, kGap, m);
                vr = m[0]; sr = m[1]; kr = m[2];
            }
            else
            {
                const int f2[2] = { 0, kAmountW }, w2[2] = { 1, 0 };
                layoutRow (area, f2, w2, 2, kGap, m);
                sr = m[0]; kr = m[1];
            }
        }
        const int sh = std::min (sr.h, tall_ ? 120 : 64);
        amount.place (kr);
        type.place ({ sr.x, sr.y + (sr.h - sh) / 2, sr.w, sh });
        if (hasVar_)
            var.place (vr);   // the whole height of the slot like the amount knob: the name and the value of both stand on the same two lines
        syncKids();
    }
    bool mirrored_, typeOff_ = false, hasVar_ = false, applies_ = true, tall_ = false;
};

/** 11b. WarpPair: two WarpSlots, the left one with the knob on the left, the right one mirrored, with the caption "WARP" between them. */
class WarpPair : public Composite
{
public:
    WarpSlot first { false }, second { true };

    WarpPair() { first.amount.setLabel ("WARP 1"); second.amount.setLabel ("WARP 2"); addKid (first); addKid (second); }   // the two knobs name their slot
    /** The stacked variant (inside a titled block of the full screen: the block's title says WARP): the first slot over the second, no caption. */
    void setStacked (bool s) { stacked_ = s; second.setMirrored (! s); layout(); }
    bool stacked() const noexcept { return stacked_; }
    /** ONE slot only (the default view of the multisample, sample and spectral types: warp 1; the second slot is behind MORE): the first slot has the whole room, the second is not shown. Stacked variant only. */
    void setSingle (bool one) { single_ = one; first.setTall (one); show (second, ! one); layout(); }
    bool single() const noexcept { return single_; }
    Size minSize() const override
    {
        const Size a = first.minSize(), b = second.minSize();
        return stacked_ ? (single_ ? a : Size { std::max (a.w, b.w), a.h + b.h + kQLane }) : Size { a.w + b.w + kCaptionW + 2 * kGap, std::max (a.h, b.h) };
    }
    Rect captionBox() const noexcept { return caption_; }

protected:
    static constexpr int kCaptionW = 64;
    void layout() override
    {
        if (stacked_ && single_)
        {
            caption_ = {};
            first.place (rect_);
            second.place ({});
            return;
        }
        if (stacked_)
        {
            const int h = std::max (0, (rect_.h - kQLane) / 2);   // kQLane between the slots: the lane of the Q index chips of the second slot's members
            caption_ = {};
            first.place ({ rect_.x, rect_.y, rect_.w, h });
            second.place ({ rect_.x, rect_.y + h + kQLane, rect_.w, std::max (0, rect_.h - h - kQLane) });
            return;
        }
        Rect r[3];
        const int fixed[3] = { 0, kCaptionW, 0 }, weight[3] = { 1, 0, 1 };
        layoutRow (rect_, fixed, weight, 3, kGap, r);
        first.place (r[0]);
        caption_ = r[1];
        second.place (r[2]);
    }
    void drawExtra (Pen& p) const override { if (! stacked_) p.textCentered (caption_, "WARP", theme::L().fLabel - 2.0f, p.c (theme::L().textDim, 255), theme::L().fMin); }
    Rect caption_;
    bool stacked_ = false, single_ = false;
};

// ===============================================================================================================================================================
/** 12. KnobRandPair: a main knob with the small secondary knob (RAND) below it, joined by a short line. turn goes to the main knob. */
class KnobRandPair : public Composite
{
public:
    Knob main;
    Knob rand { "RAND", true };

    explicit KnobRandPair (const char* label = "") { main.setLabel (label); addKid (main); addKid (rand); setPrimary (&main); }
    Size minSize() const override { return { 96, 72 + kLinkGap + 56 }; }

protected:
    static constexpr int kLinkGap = 8;
    void layout() override
    {
        const int sh = std::min (56, std::max (0, rect_.h / 2)), sw = std::min (rect_.w, 96);
        rand.place ({ rect_.x + (rect_.w - sw) / 2, rect_.bottom() - sh, sw, sh });
        main.place ({ rect_.x, rect_.y, rect_.w, std::max (0, rect_.h - sh - kLinkGap) });
    }
    void drawExtra (Pen& p) const override
    {
        const float cx = static_cast<float> (rect_.x) + static_cast<float> (rect_.w) * 0.5f;
        p.line (cx, static_cast<float> (main.rect().bottom() - 2), cx, static_cast<float> (rand.rect().y + 6), 1.5f, p.c (theme::L().keyline, 255));
    }
};


// ===============================================================================================================================================================
/** SourcePicker: the source stepper with its dropdown button (the list of all sources). turn goes to the stepper. A change of the stepper is an Index change, the dropdown a Request. */
class SourcePicker : public Composite
{
public:
    Stepper stepper;
    MenuButton dropdown { "", MenuButton::Icon::Down };

    SourcePicker() { addKid (stepper); addKid (dropdown); setPrimary (&stepper); }
    Size minSize() const override { return { stepper.minSize().w + (compact_ ? 0 : kGap + 56), 56 }; }   // the stepper's own minimum (a narrower one in the compact row of the Play page)
    /** The compact variant (the row of the Play page, where PAN and LEVEL stand beside it): no list button, the stepper has the whole width so that a table name has the room it needs; a tap on the name (`listable`) is the list / the browser. */
    void setCompact (bool c) { compact_ = c; show (dropdown, ! c); layout(); }
    bool compact() const noexcept { return compact_; }

protected:
    bool compact_ = false;
    void layout() override
    {
        if (compact_)
        {
            stepper.place (rect_);
            dropdown.place ({});
            return;
        }
        Rect r[2];
        const int fixed[2] = { 0, 56 }, weight[2] = { 1, 0 };
        layoutRow (rect_, fixed, weight, 2, kGap, r);
        stepper.place (r[0]); dropdown.place (r[1]);
    }
};

// ===============================================================================================================================================================
/** The five oscillator instances of the synth (engine keys sub, a, b, c, noise) and the types of A, B and C. Sub and Noise are instances with a fixed panel, not types. */
enum class OscInstance : uint8_t { Sub, A, B, C, Noise };
enum class OscKind : uint8_t { Wavetable, Multisample, Granular, Sample, Spectral };   // the five types of A / B / C (engine oscN.type 0, 4, 2, 1, 3)
inline const char* instanceName (OscInstance i) noexcept { static const char* const n[5] = { "SUB", "A", "B", "C", "NOISE" }; return n[static_cast<int> (i)]; }

/** BasicControls: the four values of the compact oscillator Q group "Basic". A, B, C: [type pictograms, source (stepper + dropdown), PAN, LEVEL]; Sub: [waveform pictograms (sine, half sine, triangle, saw, square, pulse), OCT, PAN,
    LEVEL]; Noise: [noise type stepper, FILTER knob, PAN, LEVEL]. `stacked`: the compact arrangement (rows one under the other) instead of one row. The group is `group` (always one frame). */
class BasicControls : public Composite
{
public:
    TypeSelector selector;   // the type or waveform: a row of cells or a drop-down in the stacked layout
    Stepper noiseType;
    SourcePicker source;
    NumberField oct { "OCT", NumberField::Format::IntSigned };
    Knob filter { "FILTER" };
    Knob pan { "PAN" }, level { "LEVEL" };
    QGroup group;

    BasicControls()
    {
        oct.state.min = -4; oct.state.max = 4;
        pan.state.bipolar = true; pan.state.defaultValue = 0.5; pan.state.value = pan.state.modValue = 0.5;
        pan.state.side = level.state.side = filter.state.side = true;
        noiseType.setCaption ("SOUND");   // which noise (white, pink ...) or which sample the noise plays; the KIND stepper of the block says noise or sample
        addKid (selector); addKid (noiseType, false); addKid (source); addKid (oct, false); addKid (filter, false);
        // Native Pan/Level retain state and Q routing, never overlay drawing or pointer input.
        configure (OscInstance::A, OscKind::Wavetable);
    }
    /** Chooses the members for an instance (and the type of A, B, C): the pictograms of the selector, the visible parts and the Q group. The states of the shown parts are the owner's. */
    void configure (OscInstance inst, OscKind kind)
    {
        inst_ = inst;
        kind_ = kind;
        const bool sub = inst == OscInstance::Sub, noise = inst == OscInstance::Noise;
        selector.state.count = 0;
        if (sub)
        {
            static const int ids[6] = { pictogram::Sine, pictogram::HalfSine, pictogram::Triangle, pictogram::Saw, pictogram::Square, pictogram::Pulse };
            static const char* const nm[6] = { "SINE", "HALF", "TRI", "SAW", "SQR", "PULSE" };
            for (int i = 0; i < 6; ++i)
                selector.setItem (i, ids[i], nm[i]);
        }
        else if (! noise)
        {
            selector.setItem (0, pictogram::Wavetable, "WAVE");
            selector.setItem (1, pictogram::Multisample, "MULTI");
            selector.setItem (2, pictogram::Sample, "SAMPLE");
            selector.setItem (3, pictogram::Granular, "GRAIN");
            selector.setItem (4, pictogram::Spectral, "SPEC");
            selector.state.index = kind == OscKind::Wavetable ? 0 : kind == OscKind::Multisample ? 1 : kind == OscKind::Sample ? 2 : kind == OscKind::Granular ? 3 : 4;
        }
        show (selector, ! noise); show (noiseType, noise);
        show (source, ! sub && ! noise); show (oct, sub); show (filter, noise);
        for (int i = 0; i < 4; ++i)
            group.set (i, nullptr);
        group.set (0, noise ? static_cast<Component*> (&noiseType) : &selector);
        group.set (1, noise ? static_cast<Component*> (&filter) : (sub ? static_cast<Component*> (&oct) : &source));
        group.set (2, &pan);
        group.set (3, &level);
        layout();
    }
    OscInstance instance() const noexcept { return inst_; }
    OscKind kind() const noexcept { return kind_; }
    /** The stacked arrangement is vertical (dial above name and value); the single row uses side knobs (dial beside name and value). */
    void setStacked (bool s)
    {
        stacked_ = s;
        pan.state.side = level.state.side = filter.state.side = ! s;
        selector.dropdown = s;
        source.stepper.minWidth = s ? 150 : Stepper::kMinW;   // the compact row holds the table stepper, PAN and LEVEL side by side: about 200 px for the stepper alone (no list button: the name is the list), 140 px of text
        source.setCompact (s);
        layout();
    }
    bool stacked() const noexcept { return stacked_; }
    /** The ids of the parts from base: selector, source stepper, dropdown, OCT, FILTER, noise type, PAN, LEVEL = base .. base + 7. */
    void assignIds (int base)
    {
        selector.setId (base); source.stepper.setId (base + 1); source.dropdown.setId (base + 2); oct.setId (base + 3);
        filter.setId (base + 4); noiseType.setId (base + 5); pan.setId (base + 6); level.setId (base + 7);
    }
    Size minSize() const override
    {
        const Component* first = inst_ == OscInstance::Noise ? static_cast<const Component*> (&noiseType) : &selector;
        const Component* second = inst_ == OscInstance::Noise ? static_cast<const Component*> (&filter) : (inst_ == OscInstance::Sub ? static_cast<const Component*> (&oct) : &source);
        const Size a = first->minSize(), b = second->minSize(), p = pan.minSize();
        if (stacked_)
            return Size { std::max ({ a.w + b.w + kGap, 2 * Size { 172, 72 }.w + kGap }), std::max (a.h, b.h) + kQLane + 72 };
        return { a.w + b.w + 2 * p.w + 3 * kGap, std::max ({ a.h, b.h, p.h }) };
    }
    void setEnabled (bool e) override { Composite::setEnabled (e); pan.setEnabled (e); level.setEnabled (e); }
    void setInactive (bool i) override { Composite::setInactive (i); pan.setInactive (i); level.setInactive (i); }

protected:
    void layout() override
    {
        Component* first = inst_ == OscInstance::Noise ? static_cast<Component*> (&noiseType) : &selector;
        Component* second = inst_ == OscInstance::Noise ? static_cast<Component*> (&filter) : (inst_ == OscInstance::Sub ? static_cast<Component*> (&oct) : &source);
        if (stacked_)
        {
            // two rows of the same height, the rows of the envelope third's knobs: row 1 the two selectors side by side (the type as a drop-down at the left, the table / content stepper or OCT at the right, 72 px high, in the middle of
            // the row), row 2 PAN and LEVEL as the side variant of the knob (the dial at the left, the name and the value beside it: the size of the envelope's knobs, dial at most 96 px); the lane of the Q chips between the rows
            const int rowH = std::max (0, (rect_.h - kQLane) / 2), row2Y = rect_.y + rowH + kQLane, cw = (rect_.w - kGap) / 2, h1 = std::min (rowH, 72), y1 = rect_.y + (rowH - h1) / 2;
            first->place ({ rect_.x, y1, cw, h1 });
            second->place ({ rect_.x + cw + kGap, y1, rect_.w - cw - kGap, h1 });
            filter.state.side = pan.state.side = level.state.side = true;
            pan.state.dialMax = level.state.dialMax = 96;
            pan.place ({ rect_.x, row2Y, cw, rowH });
            level.place ({ rect_.x + cw + kGap, row2Y, rect_.w - cw - kGap, rowH });
            return;
        }
        Rect r[4];
        const int fixed[4] = { 0, 0, 0, 0 };
        const int weight[4] = { first->minSize().w, second->minSize().w, pan.minSize().w, level.minSize().w };
        layoutRow (rect_, fixed, weight, 4, kGap, r);
        const int h1 = std::min (rect_.h, std::max (first->minSize().h, 72)), h2 = std::min (rect_.h, std::max (second->minSize().h, 72));   // the cells fill the band's height as far as 72 px
        first->place ({ r[0].x, r[0].y + (rect_.h - h1) / 2, r[0].w, h1 });
        second->place ({ r[1].x, r[1].y + (rect_.h - h2) / 2, r[1].w, h2 });
        pan.place (r[2]); level.place (r[3]);
    }
    OscInstance inst_ = OscInstance::A;
    OscKind kind_ = OscKind::Wavetable;
    bool stacked_ = false;
};

// ===============================================================================================================================================================
/** RoutePanel: the popup the RouteButton opens. The target Stepper (Filter / Main / Direct / None: the list is the owner's), the F1 <-> F2 balance Knob (bipolar; enabled only when the owner
    says the target is the filter: setTargetIsFilter), the BUS 1 and BUS 2 send Knobs and the close button (a Request change). turn goes to the target stepper. */
class RoutePanel : public Composite
{
public:
    Stepper target;
    Knob balance { "F1 F2" }, bus1 { "BUS 1" }, bus2 { "BUS 2" };
    MenuButton close { "CLOSE", MenuButton::Icon::None };
    QGroup group;   // its four values in one Q group: target, balance, BUS 1, BUS 2

    RoutePanel()
    {
        balance.state.bipolar = true; balance.state.defaultValue = 0.5; balance.state.value = balance.state.modValue = 0.5;
        addKid (target); addKid (close); addKid (balance); addKid (bus1); addKid (bus2);
        setPrimary (&target);
        group.set (0, &target); group.set (1, &balance); group.set (2, &bus1); group.set (3, &bus2);
    }
    void setTargetIsFilter (bool f) { isFilter_ = f; syncKids(); }
    bool targetIsFilter() const noexcept { return isFilter_; }
    Size minSize() const override { return { std::max (Stepper::kMinW, 3 * 100 + 2 * kGap), 56 + kGap + 56 + kGap + 72 }; }

    /** The popup variant: the panel is a framed box on a dimmed backdrop over `parent` (the rectangle of the screen below it). A tap outside the box (inside the parent) is a Request change with
        the id of `setBackdropId`: "close requested". The box itself is the rectangle given by `place`; the backdrop is the only thing drawn outside it. */
    void setPopup (bool popup, const Rect& parent) { popup_ = popup; parent_ = parent; backdropTap_ = false; }
    bool popup() const noexcept { return popup_; }
    void setBackdropId (int id) noexcept { backdropId_ = id; }
    int backdropId() const noexcept { return backdropId_; }

    void draw (gpu::SceneBuilder& sb) const override
    {
        if (popup_)
        {
            Pen pen (sb, true);
            Cell cell (sb, parent_);
            pen.rect (parent_, colour::rgba (theme::L().bg1, 190));
            pen.block (rect_, true);
        }
        Composite::draw (sb);
        if (popup_)
            group.draw (sb);
    }
    Change touchDown (int x, int y, double nowMs) override
    {
        backdropTap_ = false;
        if (popup_ && enabled_ && ! rect_.contains (x, y) && parent_.contains (x, y))
        {
            backdropTap_ = true;
            tap_.down (x, y, nowMs);
            cap_ = nullptr;
            return Change::none();
        }
        return Composite::touchDown (x, y, nowMs);
    }
    Change touchMove (int x, int y) override
    {
        if (backdropTap_)
        {
            tap_.move (x, y);
            return Change::none();
        }
        return Composite::touchMove (x, y);
    }
    Change touchMove (int x, int y, double nowMs) override
    {
        if (backdropTap_)
        {
            tap_.move (x, y);
            return Change::none();
        }
        return Composite::touchMove (x, y, nowMs);
    }
    Change touchUp (int x, int y) override
    {
        if (backdropTap_)
        {
            backdropTap_ = false;
            return tap_.up() != 0 ? Change::request (backdropId_) : Change::none();
        }
        return Composite::touchUp (x, y);
    }

protected:
    void syncKids() override { for (int i = 0; i < n_; ++i) kids_[i]->setEnabled (enabled_); balance.setEnabled (enabled_ && isFilter_); }
    void layout() override
    {
        // the close button in a row of its own at the top (so that the four values are one block), then the target stepper, then the three knobs
        const int rowH = std::min (56, rect_.h / 3);
        close.place ({ rect_.right() - 112, rect_.y, std::min (112, rect_.w), rowH });
        target.place ({ rect_.x, rect_.y + rowH + kGap, rect_.w, rowH });
        Rect k[3];
        const int f3[3] = { 0, 0, 0 }, w3[3] = { 1, 1, 1 };
        const int ky = rect_.y + 2 * (rowH + kGap);
        layoutRow ({ rect_.x, ky, rect_.w, std::max (0, rect_.bottom() - ky) }, f3, w3, 3, kGap, k);
        balance.place (k[0]); bus1.place (k[1]); bus2.place (k[2]);
        syncKids();
    }
    void drawExtra (Pen& p) const override { if (popup_) p.rrect (rect_, theme::L().rBlock, p.c (theme::L().flow[0], 255), 1.6f); }
    bool isFilter_ = true, popup_ = false, backdropTap_ = false;
    Rect parent_;
    int backdropId_ = 0;
    TapGesture tap_;
};

}} // namespace synthyboy_mpc::ui
