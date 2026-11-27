// MpcKit - PageBase: everything a full-screen plugin-drawn page does apart from choosing and placing its components. A page class derives from it, binds components to parameters
// (bind), puts them into Q groups (group), places them (layout) and, when it has a display, feeds and handles it (pullExtra / onChange / drawOver). The base does the rest: pulling the values, writing
// the changes, the touch gesture of one finger, the mouse wheel, the Q slots of the page, the list popup of a stepper.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A PageBase satisfies the contract of the page view (shell-mpc/overlay/page_view_base.h BasicPageView<Ui, NoHooks>) expects of an element:
//   static constexpr int kElementPage  (the derived class defines it)      update / draw / touch / wheel / tick / fingerDown / dropGesture / forgetQGroup / topPage / qTargets / qSlotCount
// so `mpckit::PageView<MyPage>` (kit/overlay.h) is the whole view of the page on the device; the pages are driven directly in the host tests (template-mpc/tests). The Q slots are published with the runtime's own
// QSlots (overlay/q_publish.h), as every page of SynthyBoy does.
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

#include "../overlay/q_publish.h"
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

    PageBase() = default;
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
            ui::Component* top = modalComponent();   // a popup above the page takes every touch
            ui::Component* hit = top != nullptr ? top : pick (ev.x, ev.y);
            if (hit == nullptr)
                return true;   // the page lies there, nothing on it
            if (top == nullptr)
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
        if (ui::Component* top = modalComponent())
            took = top->wheelOver (x, y, notches, ch);
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
    void forgetQGroup() noexcept { active_ = 0; qSlots_.forget(); }
    int topPage() const noexcept { return pageTop_; }
    const int* qTargets() const noexcept { return qSlots_.data(); }
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
    /** Binds a pure ControlModel (no overlay draw or touch): only registered with the binder and Q groups. */
    void bind (int id, ui::ControlModel& m, Binder::Type type, const char* key)
    {
        binder_.bind (id, &m, type, key);
    }
    /** A component that is not a parameter (a display, a button): it is drawn, touched and wheeled like the others; its changes come to onChange (). */
    void addPart (int id, ui::Component& c)
    {
        c.setId (id);
        parts_.push_back (&c);
    }
    /** TO MAKE A Q GROUP: pass up to four bound components or models in the order of the hardware knobs (Q 1 .. Q 4 of the group). The group is the active one while the player touches one of its members.
        Returns the index of the group. */
    int group (std::initializer_list<ui::ControlModel*> members)
    {
        if (numGroups_ >= kMaxGroups)
            return -1;
        ui::QGroup& g = groups_[numGroups_];
        int slot = 0;
        for (ui::ControlModel* m : members)
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
    /** A popup of the page that is open now (a ListPopup of its own, opened with stack.openDetail (id) and drawn in drawOver ()): it takes every touch and the wheel, and the Q slots are cleared while it is up. null: none. */
    virtual ui::Component* modal() noexcept { return nullptr; }

    const IParamSource& paramsOf (const IInstance& inst) const noexcept { return inst.params(); }

private:
    ui::Component* modalComponent() noexcept { return pageTop_ == kListPage ? static_cast<ui::Component*> (&list_) : modal(); }
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
                if (const ui::ControlModel* m = groups_[g].member (i); m != nullptr && m->rect().contains (x, y))
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
    /** The Q slots: the groups that have members, the active one first (QRouter::mapGroups), the other mapped groups get their block of knobs (QGroup::setBlock). The instance is told only when something changed.
        The mapping and the bookkeeping are the runtime's (overlay/q_publish.h: usedGroups, QSlots, setBlocks): the same code as every page of SynthyBoy (migration step 13 of docs/ui-components/TEMPLATE.md). */
    void publishQ (IInstance& inst)
    {
        namespace ov = synthyboy_mpc::overlay;
        int ids[kMaxGroups];
        int activePos = -1;
        const int used = ov::usedGroups (groups_, numGroups_, active_, ids, activePos);
        const bool list = modalComponent() != nullptr;
        if (list || used == 0)
        {
            int none[synthyboy_mpc::QRouter::kCount];
            for (int& v : none)
                v = synthyboy_mpc::QRouter::kNone;
            qSlots_.publishRaw (inst, none);
        }
        else
            qSlots_.publish (inst, used, activePos < 0 ? 0 : activePos, [&] (int k, int* four)
            {
                for (int i = 0; i < ui::QGroup::kSlots; ++i)
                    four[i] = binder_.paramOf (groups_[ids[k]].member (i));
            });
        for (int g = 0; g < numGroups_; ++g)
            groups_[g].setActive (! list && g == active_);
        if (list)
            ov::setBlocks (groups_, numGroups_, ids, 0, 0);   // (no group has a block while a popup is up)
        else
            ov::setBlocks (groups_, numGroups_, ids, used, activePos < 0 ? 0 : activePos);
    }

    Binder binder_;
    std::vector<ui::Component*> parts_;
    ui::QGroup groups_[kMaxGroups];
    int numGroups_ = 0, active_ = 0;
    ui::ListPopup list_;
    int listParam_ = -1, pageTop_ = 0;
    ui::Component* gesture_ = nullptr;
    bool gestureDown_ = false;
    synthyboy_mpc::overlay::QSlots qSlots_;   // the sixteen slots of the page and what was published of them
};

} // namespace mpckit
