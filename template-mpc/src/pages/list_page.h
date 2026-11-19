// MpcKit Demo - PAGE 3 "List": a Stepper whose name opens a list pop-up (ui::ListPopup), a vertical Fader, and a PRESET button with a pop-up of its own that is driven by the plugin's list
// (mpckit::IContentSource), not by a parameter.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// WHAT THIS PAGE ADDS:
//   - A STEPPER on a stepped parameter: the two outer quarters step to the previous / next choice, a tap on the middle (the name, `listable`) opens the list of all choices. The PageBase opens and
//     closes that pop-up by itself: nothing to write.
//   - A FADER (ui::Fader, a vertical level control with a scale): bound like a knob (Binder::Type::Fader).
//   - A POP-UP OF YOUR OWN: a second ui::ListPopup for a list that is not a parameter. Four things make it work: (1) a member, placed in layout (); (2) openDetail (kPresetPage) when the button is tapped
//     (onChange); (3) modal () returns the pop-up while it is open, so it takes the touches and the Q slots are cleared; (4) drawOver () draws it. The choice comes back as an Index change
//     with the id of the pop-up; the page tells the plugin (IContentSource::select) and closes the pop-up.
#pragma once

#include <vector>

#include "../../../shell-mpc/kit/kit.h"
#include "../log_compat.h"

namespace demo {

namespace ui = synthyboy_mpc::ui;

class ListPage : public mpckit::PageBase
{
public:
    static constexpr int kElementPage = 3;
    static constexpr int kPresetPage = 16;                          // the id of the detail page of the preset pop-up on the UiStack (not 15: the stepper's list)
    using Type = mpckit::Binder::Type;
    enum Id : int { kIdMode = 1, kIdLevel, kIdPresetButton = 20, kIdPresetList = 30, kIdPresetBackdrop = 31 };

    ListPage() : presetButton_ ("PRESET", ui::MenuButton::Icon::Down)
    {
        mode_.setCaption ("FILTER MODE");
        mode_.listable = true;                                      // a tap on the name opens the list pop-up
        level_.setLabel ("Level");
        bind (kIdMode, mode_, Type::Stepper, "mode");
        bind (kIdLevel, level_, Type::Fader, "level");
        addPart (kIdPresetButton, presetButton_);
        group ({ &mode_, &level_ });                                // Q-Link 1: steps the mode (a stepped target is driven in steps), Q-Link 2: the level
        preset_.setId (kIdPresetList);
        preset_.setBackdropId (kIdPresetBackdrop);
        MPCKIT_LOG ("page", DEMO_DEBUG, "list page built");
        relayout();
    }

protected:
    void layout (const ui::Rect& tab) override
    {
        mode_.place ({ 40, tab.y + 40, 460, 100 });
        presetButton_.place ({ 40, tab.y + 200, 460, 90 });
        level_.place ({ 900, tab.y + 40, 140, 540 });
        preset_.place ({ 340, tab.y + 20, 600, 8 * ui::ListPopup::kRowH + 2 * ui::ListPopup::kPad });
        preset_.setPopup (tab);
    }

    /** The preset names of the plugin into the pop-up (once per frame: the array of pointers is rebuilt only when the count changes). */
    void pullExtra (mpckit::IInstance& inst, double /*nowMs*/, const mpckit::PageInput& /*in*/) override
    {
        mpckit::IContentSource* c = inst.contentSource();
        if (c == nullptr)
            return;
        if (static_cast<int> (names_.size()) != c->count())
        {
            names_.assign (static_cast<size_t> (c->count()), nullptr);
            for (int i = 0; i < c->count(); ++i)
                names_[static_cast<size_t> (i)] = c->name (i);
        }
        preset_.state.items = names_.empty() ? nullptr : names_.data();
        preset_.state.count = static_cast<int> (names_.size());
        preset_.state.current = c->selected();
        char value[24];
        std::snprintf (value, sizeof value, "%s", c->selected() >= 0 && c->selected() < c->count() ? c->name (c->selected()) : "-");
        presetButton_.setValue (value);
    }

    void onChange (const ui::Change& ch, mpckit::IInstance& inst, mpckit::UiStack& stack) override
    {
        if (ch.id == kIdPresetButton && ch.kind == ui::ChangeKind::Request)
        {
            preset_.showCurrent();
            stack.openDetail (kPresetPage);
        }
        else if (ch.id == kIdPresetList && ch.kind == ui::ChangeKind::Index)
        {
            if (mpckit::IContentSource* c = inst.contentSource())
                c->select (ch.index);
            MPCKIT_LOG ("page", DEMO_DEBUG, "preset chosen %d", ch.index);
            stack.back();
        }
        else if (ch.id == kIdPresetBackdrop)
            stack.back();
    }

    ui::Component* modal() noexcept override { return topPage() == kPresetPage ? static_cast<ui::Component*> (&preset_) : nullptr; }
    void drawOver (mpckit::gpu::SceneBuilder& sb) const override
    {
        if (topPage() == kPresetPage)
            preset_.draw (sb);
    }

private:
    ui::Stepper mode_;
    ui::Fader level_;
    ui::MenuButton presetButton_;
    ui::ListPopup preset_;
    std::vector<const char*> names_;
};

} // namespace demo
