// MpcKit - PageBase: everything a full-screen plugin-drawn page does apart from choosing and placing its components. A page class derives from it, binds components to parameters
// (bind), puts them into Q groups (group), places them (layout) and, when it has a display, feeds and handles it (pullExtra / onChange / drawOver). The base does the rest: pulling the values, writing
// the changes, the touch gesture of one finger, the mouse wheel, the Q slots of the page, the list popup of a stepper.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A PageBase satisfies the contract PageView<Ui> (shell-mpc/overlay/page_view.h) expects of an element:
//   static constexpr int kElementPage  (the derived class defines it)      update / draw / touch / wheel / tick / fingerDown / dropGesture / forgetQGroup / topPage / qTargets / qSlotCount
// so after the migration step "PageView over IInstance" (docs/ui-components/TEMPLATE.md) `PageView<MyPage>` is the whole view. Until then the pages are driven directly (template-mpc/tests).
//
// THE PAGE GEOMETRY: the canvas is the screen, 1280 x 800 pixels; the tab is the rectangle { 0, 110, 1280, 628 }. Leave 16 pixels (ui::kQLane) above every member of a Q group: the index chip
// of the member (1 .. 4, or 5 .. 16 on the XL) is drawn there.
//
// THREADS: all methods are called from the overlay thread only.
//
// NO DEPENDENCY on the synthesizer engine of SynthyBoy.
#pragma once

#include <cstring>
#include <initializer_list>
#include <vector>

#include "../overlay/touch_decoder.h"
#include "../overlay/ui_stack.h"
#include "../q_router.h"
#include "../ui/list.h"
#include "../ui/qgroup.h"
#include "binder.h"
#include "instance.h"

namespace mpckit {

using synthyboy_mpc::overlay::TouchEvent;
using synthyboy_mpc::overlay::UiStack;

/** What the controller hands a page besides the instance: the readout targets of this view and the instance number the tab names (page::Input of page_view.h derives from this one). */
struct PageInput
{
    const float* targets = nullptr;
    int instance = 1;
};

class PageBase
{
public:
    static constexpr int kCanvasW = 1280, kCanvasH = 800, kTabY = 110, kTabH = 628;
    static constexpr int kMaxGroups = 8;
    static constexpr int kListPage = 15;                         // the id of the detail page of the list popup on the UiStack
    static constexpr int kListId = 700, kListBackdrop = 701;     // the ids the popup reports with (keep the ids of your components below 700)

    PageBase()
    {
        std::memset (target_, 0xff, sizeof target_);   // -1 everywhere
    }
    virtual ~PageBase() = default;
    PageBase (const PageBase&) = delete;
    PageBase& operator= (const PageBase&) = delete;

    // ---- the contract of PageView
    /** Reads the instance: the values into the components, the display data, the Q slots. */
    void update (IInstance& inst, const UiStack& stack, double nowMs, const PageInput& in)
    {
        const IParamSource& params = inst.params();
        if (! binder_.resolved())
            binder_.resolve (params);
        pageTop_ = stack.depth() >= 2 ? stack.id (stack.depth() - 1) : 0;
        binder_.pull (params, inst);
        pullExtra (inst, nowMs, in);
        inst.setUiFullOpen (true);
        publishQ (inst);
    }
    /** The picture of the page (the ground of the tab is drawn by the view): the components in the order they were added, the Q frame and chips, the extras, the popup. */
    void draw (gpu::SceneBuilder& sb) const
    {
        for (const ui::Component* c : parts_)
            c->draw (sb);
        for (int g = 0; g < numGroups_; ++g)
        {
            groups_[g].draw (sb);
            groups_[g].drawMapped (sb);
        }
        drawOver (sb);
        if (pageTop_ == kListPage)
            list_.draw (sb);
    }
    bool touch (const TouchEvent& ev, UiStack& stack, IInstance& inst, double nowMs)
    {
        if (ev.kind == TouchEvent::Down)
        {
            gesture_ = nullptr;
            gestureDown_ = false;
            if (! covers (ev.x, ev.y))
                return false;
            ui::Component* hit = pageTop_ == kListPage ? static_cast<ui::Component*> (&list_) : pick (ev.x, ev.y);
            if (hit == nullptr)
                return true;   // the page lies there, nothing on it
            if (pageTop_ != kListPage)
                activateGroupAt (ev.x, ev.y);
            gesture_ = hit;
            gestureDown_ = true;
            handle (hit->touchDown (ev.x, ev.y, nowMs), stack, inst);
            return true;
        }
        if (gesture_ == nullptr || ! gestureDown_)
            return false;
        if (ev.kind == TouchEvent::Move)
            handle (gesture_->touchMove (ev.x, ev.y), stack, inst);
        else
        {
            handle (gesture_->touchUp (ev.x, ev.y), stack, inst);
            gestureDown_ = false;
            gesture_ = nullptr;
        }
        return true;
    }
    /** The mouse wheel at (x, y), `notches` positive away from the user: the component under the pointer takes it as steps of its own. */
    bool wheel (int x, int y, int notches, UiStack& stack, IInstance& inst, double /*nowMs*/)
    {
        if (gestureDown_ || ! covers (x, y))
            return false;
        ui::Change ch;
        bool took = false;
        if (pageTop_ == kListPage)
            took = list_.wheelOver (x, y, notches, ch);
        else
            for (size_t i = parts_.size(); i-- > 0 && ! took;)
                took = parts_[i]->wheelOver (x, y, notches, ch);
        if (took)
            handle (ch, stack, inst);
        return took;
    }
    void tick (double nowMs, UiStack& stack, IInstance& inst)
    {
        if (gesture_ != nullptr && gestureDown_)
            handle (gesture_->tick (nowMs), stack, inst);
    }
    bool fingerDown() const noexcept { return gestureDown_; }
    void dropGesture() noexcept { gesture_ = nullptr; gestureDown_ = false; }
    /** The page comes on screen again: the first group is the active one and the Q slots are published again. */
    void forgetQGroup() noexcept { active_ = 0; published_ = false; }
    int topPage() const noexcept { return pageTop_; }
    const int* qTargets() const noexcept { return target_; }
    int qSlotCount() const noexcept { return synthyboy_mpc::QRouter::kCount; }

    // ---- access for tests
    Binder& binder() noexcept { return binder_; }
    ui::QGroup& groupAt (int g) noexcept { return groups_[g]; }
    int numGroups() const noexcept { return numGroups_; }
    int activeGroup() const noexcept { return active_; }
    ui::ListPopup& list() noexcept { return list_; }
    const std::vector<ui::Component*>& parts() const noexcept { return parts_; }

protected:
    // ---- what a page does in its constructor
    /** TO ADD A CONTROL: declare the component as a member, call bind () here with a new id (< 700) and the KEY of the parameter, then place it in layout (). */
    void bind (int id, ui::Component& c, Binder::Type type, const char* key)
    {
        binder_.bind (id, &c, type, key);
        parts_.push_back (&c);
    }
    /** A component that is not a parameter (a display, a button): it is drawn, touched and wheeled like the others; its changes come to onChange (). */
    void addPart (int id, ui::Component& c)
    {
        c.setId (id);
        parts_.push_back (&c);
    }
    /** TO MAKE A Q GROUP: pass up to four bound components in the order of the hardware knobs (Q 1 .. Q 4 of the group). The group is the active one while the player touches one of its members.
        Returns the index of the group. */
    int group (std::initializer_list<ui::Component*> members)
    {
        if (numGroups_ >= kMaxGroups)
            return -1;
        ui::QGroup& g = groups_[numGroups_];
        int slot = 0;
        for (ui::Component* m : members)
            if (slot < ui::QGroup::kSlots)
                g.set (slot++, m);
        return numGroups_++;
    }
    /** The derived page's constructor ends with relayout (): it places the popup and calls layout () with the tab. */
    void relayout()
    {
        const ui::Rect tab { 0, kTabY, kCanvasW, kTabH };
        list_.place ({ 340, kTabY + 20, 600, 8 * ui::ListPopup::kRowH + 2 * ui::ListPopup::kPad });
        list_.setPopup (tab);
        list_.setId (kListId);
        list_.setBackdropId (kListBackdrop);
        layout (tab);
    }

    // ---- what a page overrides
    /** Places every component inside `tab` (ui::Component::place), leaving the Q lane above the members of a group. */
    virtual void layout (const ui::Rect& tab) = 0;
    /** Called at every update: feed a display component from the instance (inst.param (index) and inst.params ()). */
    virtual void pullExtra (IInstance& /*inst*/, double /*nowMs*/, const PageInput& /*in*/) {}
    /** A change of a component that is not bound to a parameter (a display's edit). */
    virtual void onChange (const ui::Change& /*ch*/, IInstance& /*inst*/, UiStack& /*stack*/) {}
    /** Extra drawing above the components and below the popup. */
    virtual void drawOver (gpu::SceneBuilder& /*sb*/) const {}

    const IParamSource& paramsOf (const IInstance& inst) const noexcept { return inst.params(); }

private:
    bool covers (int x, int y) const noexcept { return ui::Rect { 0, kTabY, kCanvasW, kTabH }.contains (x, y); }
    ui::Component* pick (int x, int y) const noexcept
    {
        for (size_t i = parts_.size(); i-- > 0;)
            if (parts_[i]->enabled() && parts_[i]->rect().contains (x, y))
                return parts_[i];
        return nullptr;
    }
    void activateGroupAt (int x, int y) noexcept
    {
        for (int g = 0; g < numGroups_; ++g)
            for (int i = 0; i < ui::QGroup::kSlots; ++i)
                if (const ui::Component* m = groups_[g].member (i); m != nullptr && m->rect().contains (x, y))
                    active_ = g;
    }
    void handle (const ui::Change& ch, UiStack& stack, IInstance& inst)
    {
        if (ch.kind == ui::ChangeKind::None)
            return;
        const IParamSource& params = inst.params();
        if (ch.id == kListBackdrop)
        {
            stack.back();
            return;
        }
        if (ch.id == kListId && ch.kind == ui::ChangeKind::Index)
        {
            if (listParam_ >= 0)
                Binder::writePlain (params, inst, listParam_, params.desc (listParam_).min + static_cast<float> (ch.index));
            stack.back();
            return;
        }
        const Binder::Item* it = binder_.item (ch.id);
        if (it != nullptr && it->type == Binder::Type::Stepper && ch.kind == ui::ChangeKind::Request)
        {
            openList (it->param, inst, stack);
            return;
        }
        if (! binder_.apply (ch, params, inst))
            onChange (ch, inst, stack);
    }
    /** A tap on the name of a stepper opens the list of all its choices over the page. */
    void openList (int param, IInstance& inst, UiStack& stack)
    {
        if (param < 0)
            return;
        const ParamDesc& p = inst.params().desc (param);
        if (p.numChoices <= 0 || p.choices == nullptr)
            return;
        listParam_ = param;
        list_.state.items = p.choices;
        list_.state.count = p.numChoices;
        list_.state.current = static_cast<int> (std::lround (inst.param (param) - p.min));
        list_.showCurrent();
        stack.openDetail (kListPage);
        pageTop_ = kListPage;
    }
    /** The Q slots: the groups that have members, the active one first (QRouter::mapGroups), the other mapped groups get their block of knobs (QGroup::setBlock). The instance is told only when something changed. */
    void publishQ (IInstance& inst)
    {
        int ids[kMaxGroups];
        int used = 0, activePos = -1;
        for (int g = 0; g < numGroups_; ++g)
            if (groups_[g].numMembers() > 0)
            {
                if (g == active_)
                    activePos = used;
                ids[used++] = g;
            }
        int t[synthyboy_mpc::QRouter::kCount];
        const bool list = pageTop_ == kListPage;
        if (list || used == 0)
            for (int& v : t)
                v = synthyboy_mpc::QRouter::kNone;
        else
            synthyboy_mpc::QRouter::mapGroups (used, activePos < 0 ? 0 : activePos, [&] (int k, int* four)
            {
                for (int i = 0; i < ui::QGroup::kSlots; ++i)
                    four[i] = binder_.paramOf (groups_[ids[k]].member (i));
            }, t);
        if (! published_ || std::memcmp (t, target_, sizeof target_) != 0)
        {
            std::memcpy (target_, t, sizeof target_);
            inst.setQGroup (t, synthyboy_mpc::QRouter::kCount);
            published_ = true;
        }
        for (int g = 0; g < numGroups_; ++g)
        {
            groups_[g].setBlock (-1);
            groups_[g].setActive (! list && g == active_);
        }
        for (int k = 0; k < used; ++k)
            groups_[ids[k]].setBlock (list ? -1 : synthyboy_mpc::QRouter::blockOfGroup (used, activePos < 0 ? 0 : activePos, k));
    }

    Binder binder_;
    std::vector<ui::Component*> parts_;
    ui::QGroup groups_[kMaxGroups];
    int numGroups_ = 0, active_ = 0;
    ui::ListPopup list_;
    int listParam_ = -1, pageTop_ = 0;
    ui::Component* gesture_ = nullptr;
    bool gestureDown_ = false;
    int target_[synthyboy_mpc::QRouter::kCount];
    bool published_ = false;
};

} // namespace mpckit
