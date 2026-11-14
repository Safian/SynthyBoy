// SynthyBoy - MPC UI component library: the COMPOSITE components, built only from the base components (base.h) and with the same contract (core.h): TuneRow, PhaseRow, UnisonBox, WarpSlot,
// WarpPair, KnobRandPair. The ids of the children are given by the owner (`child.setId`); a change comes out with the id of the child it came from. A composite places its children
// inside its own rectangle (place), disables them with itself, and routes the touch of a gesture to the child it went down on until the touch up (also outside the rectangle).
// turn (delta) goes to the PRIMARY child of the composite (UnisonBox: the up / down stepper, WarpSlot and KnobRandPair: the knob), the others have none.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "base.h"

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
    static constexpr int kMaxKids = 40;

    void place (const Rect& r) override { rect_ = r; layout(); }
    void setEnabled (bool e) override { enabled_ = e; cap_ = nullptr; syncKids(); }
    void setQ (bool frame, int) override { qFrame_ = frame; qIndex_ = 0; for (int i = 0; i < n_; ++i) kids_[i]->setQ (frame, 0); }

    void draw (gpu::SceneBuilder& sb) const override
    {
        for (int i = 0; i < n_; ++i)
            if (shown_[i])
                kids_[i]->draw (sb);
        Pen pen (sb, enabled_);
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
    Change touchUp (int x, int y) override
    {
        Component* k = cap_;
        cap_ = nullptr;
        return k ? k->touchUp (x, y) : Change::none();
    }
    Change tick (double nowMs) override { return cap_ ? cap_->tick (nowMs) : Change::none(); }
    Change turn (double delta) override { return enabled_ && primary_ ? primary_->turn (delta) : Change::none(); }
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
    int numChildren() const noexcept { return n_; }
    Component* child (int i) const noexcept { return kids_[i]; }
    bool childShown (int i) const noexcept { return shown_[i]; }

protected:
    virtual void layout() = 0;
    virtual void drawExtra (Pen&) const {}
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
/** 8. TuneRow: OCT, SEM, FIN, CRS number fields and the pitch tracking toggle (after CRS); the short variant (the sub oscillator) has OCT and CRS only. The defaults of the fields are the
    labels, the signed integer format (CRS: two decimals) and ranges the owner may overwrite (`oct.state` ...). */
class TuneRow : public Composite
{
public:
    NumberField oct { "OCT", NumberField::Format::IntSigned }, sem { "SEM", NumberField::Format::IntSigned }, fin { "FIN", NumberField::Format::IntSigned }, crs { "CRS", NumberField::Format::Decimal2 };
    ToggleIcon track;

    explicit TuneRow (bool shortVariant = false) : short_ (shortVariant)
    {
        oct.state.min = -4; oct.state.max = 4;
        sem.state.min = -12; sem.state.max = 12;
        fin.state.min = -100; fin.state.max = 100;
        crs.state.min = -48; crs.state.max = 48; crs.state.step = 0.01; crs.state.pxPerStep = 2;
        track.state.icon = ToggleIcon::Icon::PitchTrack;
        track.state.on = true;
        addKid (oct); addKid (sem); addKid (fin); addKid (crs); addKid (track);
        applyVariant();
    }
    void setShort (bool s) { short_ = s; applyVariant(); layout(); }
    bool isShort() const noexcept { return short_; }
    Size minSize() const override { return short_ ? Size { 2 * 80 + kGap, 56 } : Size { 4 * 80 + 56 + 4 * kGap, 56 }; }

protected:
    void applyVariant() { show (sem, ! short_); show (fin, ! short_); show (track, ! short_); }
    void layout() override
    {
        Rect r[5];
        if (short_)
        {
            const int fixed[2] = { 0, 0 }, weight[2] = { 1, 1 };
            layoutRow (rect_, fixed, weight, 2, kGap, r);
            oct.place (r[0]); crs.place (r[1]);
        }
        else
        {
            const int fixed[5] = { 0, 0, 0, 0, 56 }, weight[5] = { 1, 1, 1, 1, 0 };
            layoutRow (rect_, fixed, weight, 5, kGap, r);
            oct.place (r[0]); sem.place (r[1]); fin.place (r[2]); crs.place (r[3]); track.place (r[4]);
        }
    }
    bool short_;
};

// ===============================================================================================================================================================
/** 9. TypeBar: the strip under the oscillator display, whose content depends on the oscillator type: 1 to 4 slots left to right in a fixed height rectangle. The slots of the preset are the
    filled ones and form ONE Q group (`group`: the filled slots in order; empty slots draw nothing and take no Q-Link). The presets are DATA (a table of BarItem, the same mechanism as the
    KnobPanel's): wavetable (the frame number, the phase, RAND, the phase memory), granular (crossfade, grain window, loop mode, unison), sub (the phase), multisample (VEL TRACK toggle,
    its amount, RAND), noise (STEREO). The ids of the slots are given by the owner (`slot (i)->setId`). The former PhaseRow is the wavetable preset of this component. */
enum class BarKind : uint8_t { Frame, Phase, Rand, Number, Stepper, GrainWindow, UnisonDetail, Toggle };
struct BarItem { BarKind kind; const char* label; uint8_t weight; };
struct BarPreset { const char* name; const BarItem* items; int numItems; };

namespace barpresets
{
constexpr BarItem kWavetableItems[] = { { BarKind::Frame, "", 2 }, { BarKind::Phase, "", 2 }, { BarKind::Rand, "RAND", 2 }, { BarKind::Stepper, "MEMORY", 3 } };
constexpr BarItem kGranularItems[] = { { BarKind::Number, "XFADE", 2 }, { BarKind::GrainWindow, "AMOUNT", 3 }, { BarKind::Stepper, "LOOP", 3 }, { BarKind::UnisonDetail, "UNISON", 3 } };
constexpr BarItem kSubItems[] = { { BarKind::Phase, "", 1 } };
constexpr BarItem kMultisampleItems[] = { { BarKind::Toggle, "VEL TRACK", 1 }, { BarKind::Number, "AMOUNT", 2 }, { BarKind::Rand, "RAND", 2 } };
constexpr BarItem kNoiseItems[] = { { BarKind::Number, "STEREO", 1 } };
constexpr BarPreset kWavetable { "wavetable", kWavetableItems, 4 }, kGranular { "granular", kGranularItems, 4 }, kSub { "sub", kSubItems, 1 },
                    kMultisample { "multisample", kMultisampleItems, 3 }, kNoise { "noise", kNoiseItems, 1 };
}

class TypeBar : public Composite
{
public:
    static constexpr int kMaxSlots = 4;
    NumberField numbers[kMaxSlots];
    Stepper steppers[kMaxSlots];
    DetailButton details[kMaxSlots];
    ToggleIcon toggles[kMaxSlots];

    TypeBar() = default;
    explicit TypeBar (const BarPreset& p) { load (p); }

    /** Switches to a preset: the slot components come back to their defaults. */
    void load (const BarPreset& p)
    {
        clearKids();
        preset_ = &p;
        n_slots_ = 0;
        for (int i = 0; i < p.numItems && i < kMaxSlots; ++i)
        {
            const BarItem& it = p.items[i];
            Component* c = nullptr;
            switch (it.kind)
            {
                case BarKind::Frame:
                    numbers[i].state = NumberField::State {};
                    numbers[i].state.format = NumberField::Format::IntPlain; numbers[i].state.readOnly = true; numbers[i].state.max = 255;
                    c = &numbers[i];
                    break;
                case BarKind::Phase:
                    numbers[i].state = NumberField::State {};
                    numbers[i].state.format = NumberField::Format::Degrees; numbers[i].state.icon = NumberField::Icon::Phase; numbers[i].state.max = 360;
                    c = &numbers[i];
                    break;
                case BarKind::Rand:
                    numbers[i].state = NumberField::State {};
                    numbers[i].state.format = NumberField::Format::IntPlain; numbers[i].state.icon = NumberField::Icon::Rand; numbers[i].state.max = 100;
                    numbers[i].setLabel (it.label);
                    c = &numbers[i];
                    break;
                case BarKind::Number:
                    numbers[i].state = NumberField::State {};
                    numbers[i].state.format = NumberField::Format::IntPlain; numbers[i].state.max = 100;
                    numbers[i].setLabel (it.label);
                    c = &numbers[i];
                    break;
                case BarKind::Stepper:
                    steppers[i].state = Stepper::State {};
                    steppers[i].state.wrap = true;
                    steppers[i].setItem (it.label);
                    c = &steppers[i];
                    break;
                case BarKind::GrainWindow:
                    details[i].state = DetailButton::State {};
                    details[i].state.icon = DetailButton::Icon::Bell; details[i].state.hasValue = true; details[i].setLabel (it.label);
                    c = &details[i];
                    break;
                case BarKind::UnisonDetail:
                    details[i].state = DetailButton::State {};
                    details[i].state.icon = DetailButton::Icon::Gear; details[i].state.hasValue = true; details[i].state.integer = true;
                    details[i].state.min = 1; details[i].state.max = 16; details[i].state.value = 1; details[i].setLabel (it.label);
                    c = &details[i];
                    break;
                case BarKind::Toggle:
                    toggles[i].state = ToggleIcon::State {};
                    toggles[i].state.icon = ToggleIcon::Icon::Generic;
                    c = &toggles[i];
                    break;
            }
            c->setId (0);
            slots_[n_slots_++] = c;
            addKid (*c);
        }
        layout();
    }
    const BarPreset* preset() const noexcept { return preset_; }
    int numSlots() const noexcept { return n_slots_; }
    Component* slot (int i) const noexcept { return i >= 0 && i < n_slots_ ? slots_[i] : nullptr; }
    Size minSize() const override
    {
        int w = 0;
        for (int i = 0; i < n_slots_; ++i)
            w += slots_[i]->minSize().w + (i ? kGap : 0);
        return { w, 56 };
    }

protected:
    void layout() override
    {
        if (n_slots_ == 0)
            return;
        int fixed[kMaxSlots] = {}, weight[kMaxSlots] = {};
        Rect r[kMaxSlots];
        for (int i = 0; i < n_slots_; ++i)
            weight[i] = slots_[i]->minSize().w;   // in proportion to the minimum widths: at the minimum size every slot has its own minimum
        layoutRow (rect_, fixed, weight, n_slots_, kGap, r);
        for (int i = 0; i < n_slots_; ++i)
            slots_[i]->place (r[i]);
    }
    const BarPreset* preset_ = nullptr;
    Component* slots_[kMaxSlots] = {};
    int n_slots_ = 0;
};

// ===============================================================================================================================================================
/** 10. UnisonBox: the settings (gear) button and the up / down stepper (the voice count). turn goes to the stepper. */
class UnisonBox : public Composite
{
public:
    DetailButton gear { DetailButton::Icon::Gear };
    UpDownStepper count;

    UnisonBox() { count.state.min = 1; count.state.max = 16; count.state.value = 1; addKid (gear); addKid (count); setPrimary (&count); }
    Size minSize() const override { return { 56 + kGap + 72, 112 }; }

protected:
    void layout() override
    {
        Rect r[2];
        const int fixed[2] = { 56, 0 }, weight[2] = { 0, 1 };
        layoutRow (rect_, fixed, weight, 2, kGap, r);
        gear.place (r[0]); count.place (r[1]);
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
    Knob var { "VAR", true };

    explicit WarpSlot (bool mirrored = false) : mirrored_ (mirrored) { addKid (amount); addKid (type); addKid (var, false); setPrimary (&amount); }
    void setTypeOff (bool off) { typeOff_ = off; type.state.dim = off; syncKids(); }
    void setHasVar (bool has) { hasVar_ = has; show (var, has); layout(); }
    bool typeOff() const noexcept { return typeOff_; }
    bool hasVar() const noexcept { return hasVar_; }
    bool mirrored() const noexcept { return mirrored_; }
    Size minSize() const override { return { 100 + kGap + Stepper::kMinW + (hasVar_ ? kGap + 80 : 0), 72 }; }

protected:
    void syncKids() override { amount.setEnabled (enabled_ && ! typeOff_); var.setEnabled (enabled_ && ! typeOff_); type.setEnabled (enabled_); }
    void layout() override
    {
        // order of the pieces from the left: knob, stepper, var; mirrored: var, stepper, knob
        Rect r[3];
        const int fixed[3] = { 100, 0, 80 }, weight[3] = { 0, 1, 0 };
        const int n = hasVar_ ? 3 : 2;
        layoutRow (rect_, fixed, weight, n, kGap, r);
        Rect kr = r[0], sr = r[1], vr = hasVar_ ? r[2] : Rect {};
        if (mirrored_)
        {
            // the same pieces, from the right: build from the mirrored positions
            Rect m[3];
            const int mf[3] = { 80, 0, 100 }, mw[3] = { 0, 1, 0 };
            if (hasVar_)
            {
                layoutRow (rect_, mf, mw, 3, kGap, m);
                vr = m[0]; sr = m[1]; kr = m[2];
            }
            else
            {
                const int f2[2] = { 0, 100 }, w2[2] = { 1, 0 };
                layoutRow (rect_, f2, w2, 2, kGap, m);
                sr = m[0]; kr = m[1];
            }
        }
        const int sh = std::min (sr.h, 64);
        amount.place (kr);
        type.place ({ sr.x, sr.y + (sr.h - sh) / 2, sr.w, sh });
        if (hasVar_)
            var.place (vr);
        syncKids();
    }
    bool mirrored_, typeOff_ = false, hasVar_ = false;
};

/** 11b. WarpPair: two WarpSlots, the left one with the knob on the left, the right one mirrored, with the caption "WARP" between them. */
class WarpPair : public Composite
{
public:
    WarpSlot first { false }, second { true };

    WarpPair() { addKid (first); addKid (second); }
    Size minSize() const override { const Size a = first.minSize(), b = second.minSize(); return { a.w + b.w + kCaptionW + 2 * kGap, std::max (a.h, b.h) }; }
    Rect captionBox() const noexcept { return caption_; }

protected:
    static constexpr int kCaptionW = 64;
    void layout() override
    {
        Rect r[3];
        const int fixed[3] = { 0, kCaptionW, 0 }, weight[3] = { 1, 0, 1 };
        layoutRow (rect_, fixed, weight, 3, kGap, r);
        first.place (r[0]);
        caption_ = r[1];
        second.place (r[2]);
    }
    void drawExtra (Pen& p) const override { p.textCentered (caption_, "WARP", 2, p.c (colour::kText, 160)); }
    Rect caption_;
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
        p.line (cx, static_cast<float> (main.rect().bottom() - 2), cx, static_cast<float> (rand.rect().y + 6), 2.0f, p.c (colour::kRest, 255));
    }
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

    RoutePanel()
    {
        balance.state.bipolar = true; balance.state.defaultValue = 0.5; balance.state.value = balance.state.modValue = 0.5;
        addKid (target); addKid (close); addKid (balance); addKid (bus1); addKid (bus2);
        setPrimary (&target);
    }
    void setTargetIsFilter (bool f) { isFilter_ = f; syncKids(); }
    bool targetIsFilter() const noexcept { return isFilter_; }
    Size minSize() const override { return { std::max (Stepper::kMinW + kGap + 112, 3 * 100 + 2 * kGap), 56 + kGap + 72 }; }

protected:
    void syncKids() override { for (int i = 0; i < n_; ++i) kids_[i]->setEnabled (enabled_); balance.setEnabled (enabled_ && isFilter_); }
    void layout() override
    {
        const int topH = std::min (56, rect_.h / 2);
        Rect r[2];
        const int fixed[2] = { 0, 112 }, weight[2] = { 1, 0 };
        layoutRow ({ rect_.x, rect_.y, rect_.w, topH }, fixed, weight, 2, kGap, r);
        target.place (r[0]); close.place (r[1]);
        Rect k[3];
        const int f3[3] = { 0, 0, 0 }, w3[3] = { 1, 1, 1 };
        layoutRow ({ rect_.x, rect_.y + topH + kGap, rect_.w, std::max (0, rect_.h - topH - kGap) }, f3, w3, 3, kGap, k);
        balance.place (k[0]); bus1.place (k[1]); bus2.place (k[2]);
        syncKids();
    }
    void drawExtra (Pen& p) const override { p.frame (rect_, 1, p.c (colour::kRest, 255)); }
    bool isFilter_ = true;
};

}} // namespace synthyboy_mpc::ui
