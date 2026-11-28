// SynthyBoy - MPC overlay: a page of the OSC tab, the element PageView (page_view.h) shows for the views Osc A, Osc B, Osc C, Sub and Noise (overlay_layout.json, kind "oscui"; the view's index is the
// oscillator selector's number: 1 Sub, 2 A, 3 B, 4 C, 5 Noise). It is the full screen of the oscillator UI (osc_ui.h: the same class the Play page opens with its full screen button, in its page mode) with
// the display of the oscillator (osc_display.h: the same class the Play view draws its picture with): the Basic group, the type bar, the tuning column, the knob panel with its Q groups, the detail
// pages (unison, grain window, envelope, modulations), the route popup, the lists and the content browser, and the live read-outs. The tab names the default source; the native selector changes only the open page.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <utility>

#ifdef SYNTHYBOY_BACKGROUNDS_MANIFEST_H
#include SYNTHYBOY_BACKGROUNDS_MANIFEST_H
#else
#include "backgrounds_manifest.h"
#endif
#include "native_marker_covers.h"
#include "osc_display.h"
#include "osc_marks.h"
#include "osc_ui.h"
#include "page_view.h"
#include "../ui/native_apertures.h"

namespace synthyboy_mpc { namespace overlay {

class OscPageUi
{
public:
    static constexpr int kElementPage = OscUi::kElementOsc;
    static constexpr ui::Rect expandedCloseRect() noexcept { return { 1148, page::kTabY + 116, 120, 56 }; }

    OscPageUi() : ownedOsc_ (std::make_unique<OscUi>()), ownedDisplay_ (std::make_unique<OscDisplay>()),
                  osc_ (*ownedOsc_), display_ (*ownedDisplay_) {}
    OscPageUi (OscUi& osc, OscDisplay& display) : osc_ (osc), display_ (display), inPlay_ (true) {}

    void enterPlay() noexcept { setHostBackground (false); }

    void leavePlay (IInstanceView& inst)
    {
        leftTab (inst);
        osc_.setPage (0);
        setNativeInstanceSelector (false);
        setNativeRouting (false);
        osc_.full().setNativePower (false);
        sel_ = tabSelection_ = 0;
    }

    /** The same three oscillator readout slices as Play, shared by every standalone tab. */
    static std::vector<int> readoutTargets()
    {
        std::vector<int> out;
        OscDisplay::targets (out);
        return out;
    }

    void update (IInstanceView& inst, UiStack& stack, double nowMs, const page::Input& in)
    {
        const auto generation = inst.stateGeneration();
        const int tabSelection = std::clamp (inPlay_ ? inst.playSelection (0) : in.instance, 1, playosc::kInstances);
        if (tabSelection != tabSelection_ || generation != preset_)
        {
            const bool keepDetail = tabSelection == tabSelection_ && sel_ == tabSelection
                                   && isInstanceOn (inst, tabSelection) && osc_.canKeepDetailOnRecall (inst, tabSelection);
            tabSelection_ = tabSelection;
            preset_ = generation;
            osc_.dropGesture();
            hideNativeSettings (inst);
            hideNativeDetail (inst);
            setSelection (tabSelection_);
            if (! keepDetail && stack.element() == kElementPage && stack.depth() > 1)
                stack.openFull (kElementPage);
        }
        updateNative (inst, stack);
        if (inst.nativeOscDetailSupported() && ! isInstanceOn (inst, sel_) && stack.depth() > 1)
        {
            stack.openFull (kElementPage);
            hideNativeDetail (inst);
        }
        auto pull = [&] { osc_.update (inst, 1, stack, nowMs, std::clamp (inst.playSelection (1), 1, 6), std::clamp (inst.playSelection (2), 1, 10), inPlay_); };
        pull();
        if (updateNativeSettings (inst, stack, true))
        {
            if (! nativeRoot (stack))
                hideNative (inst);
            pull();
            if (! nativePublished_ && nativeRoot (stack))
                updateNative (inst, stack);
            updateNativeSettings (inst, stack, false);
        }
        if (updateNativeDetail (inst, stack, true))
        {
            if (! nativeRoot (stack))
            {
                hideNative (inst);
                hideNativeSettings (inst);
            }
            pull();
            if (! nativePublished_ && nativeRoot (stack))
                updateNative (inst, stack);
            updateNativeSettings (inst, stack, false);
            updateNativeDetail (inst, stack, false);
        }
        osc_.syncNativeModulations (inst, stack, inPlay_ ? nativeui::targets::PlayOsc : nativeui::targets::Osc);
        marks_ = OscMarks::of (inst, sel_, osc_.topPage());
        display_.update (inst, sel_, in.targets, in.live, nowMs, counters_);
        inst.setNativeText (nativeui::nativetext::OscMessage, display_.message());
    }

    /** The picture: the page, the oscillator's display in its rectangle (the data under the live read-outs), the popups over it. */
    void draw (gpu::SceneBuilder& sb) const
    {
        if (hostBackground())
            drawHostCovers (sb);
        osc_.drawPage (sb);
        const ui::Rect r = osc_.displayRect();
        if (! r.empty() && display_.valid())
        {
            const bool nativeRootPower = osc_.topPage() == 0 && osc_.full().hasNativePower();
            const std::array<ui::Rect, 3> header { powerArea(), inPlay_ ? expandedCloseRect() : ui::Rect {}, osc_.full().isNativeRoutingActive() ? routingArea() : ui::Rect {} };
            display_.draw (sb, r, false, osc_.plotArea(), nativeRootPower ? &header : nullptr);
            display_.draw (sb, r, true, osc_.plotArea());
            if (marks_.any())
                marks_.draw (sb, r);
        }
    }

    void drawHostCovers (gpu::SceneBuilder& sb) const
    {
        for (const auto& view : layout::kViews)
            if (view.kind == layout::kKindOscUi && view.index == tabSelection_)
            {
                drawNativeMarkerCovers (sb, view.name);   // the markers and the sentinel (tab y 8)
                return;
            }
    }

    bool covers (int x, int y) const noexcept
    {
        if (inActiveAperture (x, y))
            return false;
        return osc_.covers (x, y);
    }
    bool touch (const TouchEvent& ev, UiStack& stack, IInstanceView& inst, double nowMs)
    {
        if (ev.kind != TouchEvent::Down && nativePointer_)
        {
            if (ev.kind == TouchEvent::Up)
            {
                nativePointer_ = false;
                nativePress_ = {-1, -1};
            }
            else if (std::abs (ev.x - pressX_) + std::abs (ev.y - pressY_) > 6)
                nativePress_ = {-1, -1};
            return false;
        }
        if (ev.kind == TouchEvent::Down)
        {
            nativePointer_ = false;
            nativePress_ = {-1, -1};
            nativeMenu_ = -1;
        }
        if (ev.kind == TouchEvent::Down && inActiveAperture (ev.x, ev.y))
        {
            if (osc_.topPage() == 0 && osc_.full().isNativeRouting (ev.x, ev.y))
            {
                osc_.focusNativeMix (inst);
                nativePress_ = osc_.nativeModTarget (ev.x, ev.y);
                nativePointer_ = true;
                pressX_ = ev.x;
                pressY_ = ev.y;
                pressAt_ = nowMs;
                return false;
            }
            if (isNativeTuneActive() && tuneArea().contains (ev.x, ev.y))
            {
                const auto r = tuneArea();
                if (! osc_.full().inactive())
                    osc_.focusNativeTune (ev.y >= r.y + 98 && ev.x < r.x + 143, inst);
                nativePress_ = osc_.nativeModTarget (ev.x, ev.y);
                nativePointer_ = true;
                pressX_ = ev.x;
                pressY_ = ev.y;
                pressAt_ = nowMs;
                return false;
            }
            if (inSettingAperture (ev.x, ev.y))
                osc_.focusNativeSetting (ev.x, ev.y, inst);
            const int field = osc_.nativeDetailFieldAt (ev.x, ev.y);
            osc_.focusNativeDetail (field, inst);
            nativeMenu_ = osc_.nativeDetailMenu (field);
            nativePress_ = osc_.nativeModTarget (ev.x, ev.y);
            nativePointer_ = nativePress_.first >= 0;
            pressX_ = ev.x;
            pressY_ = ev.y;
            pressAt_ = nowMs;
            return false;
        }
        const bool taken = osc_.touch (ev, stack, inst, nowMs);
        if (! nativeRoot (stack))
        {
            hideNative (inst);
            hideNativeSettings (inst);
        }
        return taken;
    }
    bool wheel (int x, int y, int notches, UiStack& stack, IInstanceView& inst, double nowMs)
    {
        if (inActiveAperture (x, y))
            return false;
        return osc_.wheel (x, y, notches, stack, inst, nowMs);
    }
    void tick (double nowMs, UiStack& stack, IInstanceView& inst)
    {
        if (nativePress_.first >= 0 && nowMs - pressAt_ >= 600.0)
        {
            const auto target = std::exchange (nativePress_, std::pair<int, int> {-1, -1});
            if (inst.stateGeneration() == preset_ && isInstanceOn (inst, sel_) && target == osc_.nativeModTarget (pressX_, pressY_))
            {
                osc_.openNativeMods (target.first, stack, inst);
                hideNativeDetail (inst);
            }
        }
        osc_.tick (nowMs, stack, inst);
        if (! nativeRoot (stack))
        {
            hideNative (inst);
            hideNativeSettings (inst);
        }
    }
    bool fingerDown() const noexcept { return osc_.fingerDown() || nativePress_.first >= 0; }
    void dropGesture() noexcept { osc_.dropGesture(); nativePress_ = {-1, -1}; nativePointer_ = false; nativeMenu_ = -1; }
    void setNativeActionPopup (IInstanceView& inst, int action) noexcept
    {
        if (action == -1 || ! nativeModal_)
            inst.setNativeOscDetailMenu (action == -1 ? -1 : nativeMenu_ >= 0 ? nativeMenu_ : -2);
        nativeModal_ = action != -1;
    }
    void forgetQGroup() noexcept { osc_.forgetQGroup(); }
    void leftTab (IInstanceView& inst)
    {
        setNativeActionPopup (inst, -1);
        osc_.hideNativeModulations (inst);
        dropGesture();
        hideNative (inst);
        hideNativeSettings (inst);
        hideNativeDetail (inst);
        setSelection (tabSelection_);
        osc_.closePopups();
    }
    int topPage() const noexcept { return osc_.topPage(); }
    const int* qTargets() const noexcept { return osc_.qTargets(); }
    int qSlotCount() const noexcept { return QRouter::kCount; }

    // ---- access for the tests
    OscUi& ui() noexcept { return osc_; }
    const OscUi& ui() const noexcept { return osc_; }
    const OscDisplay& display() const noexcept { return display_; }
    int selection() const noexcept { return sel_; }

    // ---- standalone native instance choice and OFF navigation
    void setSelection (int sel)
    {
        if (sel >= 1 && sel <= playosc::kInstances && sel != sel_)
        {
            osc_.dropGesture();
            sel_ = sel;
            osc_.setPage (sel_);
        }
    }
    static const char* const* instanceNames() noexcept
    {
        static_assert (playosc::kInstances == nativeui::osc::kLastSource);
        return nativeui::osc::kSourceNames;
    }
    static const char* instanceName (int sel) noexcept
    {
        return sel >= 1 && sel <= playosc::kInstances ? instanceNames()[sel - 1] : "";
    }
    static int powerParamIndex (int sel) noexcept
    {
        static const char* const keys[playosc::kInstances] { "sub.on", "osc1.on", "osc2.on", "osc3.on", "noise.on" };
        if (sel < 1 || sel > playosc::kInstances)
            return -1;
        const auto* p = synthyboy::findParamByKey (keys[sel - 1]);
        return p ? static_cast<int> (p - synthyboy::paramTable()) : -1;
    }
    static bool isInstanceOn (const IInstanceView& inst, int sel)
    {
        const int idx = powerParamIndex (sel);
        return idx >= 0 && inst.param (idx) >= 0.5f;
    }
    static std::string choiceLabel (const IInstanceView& inst, int sel)
    {
        const char* name = instanceName (sel);
        if (! name || name[0] == '\0')
            return "";
        const bool on = isInstanceOn (inst, sel);
        return std::string (name) + (on ? " (Active)" : " (Inactive)");
    }
    std::string currentChoiceLabel (const IInstanceView& inst) const
    {
        return choiceLabel (inst, sel_);
    }
    void setNativeInstanceSelector (bool shown) noexcept
    {
        nativeSelector_ = shown;
        osc_.setNativeInstanceSelector (shown);
    }
    bool hasNativeInstanceSelector() const noexcept { return nativeSelector_; }
    void setNativeRouting (bool shown) noexcept
    {
        nativeRouting_ = shown;
        osc_.setNativeRouting (shown);
    }
    bool hasNativeRouting() const noexcept { return nativeRouting_; }
    bool isNativeRoutingActive() const noexcept { return nativeRouting_ && osc_.isNativeRoutingActive(); }
    static constexpr int kDestinationChoiceCount = nativeui::osc::kLastRoute + 1;
    static const char* destinationKey (int sel) noexcept
    {
        static const char* const keys[playosc::kInstances] = {
            "sub.destination", "osc1.destination", "osc2.destination", "osc3.destination", "noise.destination"
        };
        return sel >= 1 && sel <= playosc::kInstances ? keys[sel - 1] : "";
    }
    static int destinationParamIndex (int sel) noexcept
    {
        const char* key = destinationKey (sel);
        if (! key || key[0] == '\0')
            return -1;
        const auto* p = synthyboy::findParamByKey (key);
        return p ? static_cast<int> (p - synthyboy::paramTable()) : -1;
    }
    static int currentDestination (const IInstanceView& inst, int sel) noexcept
    {
        const int idx = destinationParamIndex (sel);
        if (idx < 0)
            return -1;
        return std::clamp (static_cast<int> (std::lround (inst.param (idx))), 0, kDestinationChoiceCount - 1);
    }
    bool hostBackground() const noexcept
    {
        return nativeGroundReady_ && osc_.topPage() == 0
            && &ui::theme::L() == &ui::theme::kLook1;
    }
    // The page owns the ground of its fallback and the marker covers of its native root.
    void drawPageGround (gpu::SceneBuilder&) const noexcept {}
    void setHostBackground (bool on) noexcept
    {
        nativeGroundReady_ = on;
        setNativeInstanceSelector (true);
        setNativeRouting (true);
        osc_.full().setNativePower (true);
    }
    ui::Rect selectorArea() const noexcept { return osc_.selectorArea(); }
    ui::Rect powerArea() const noexcept { return osc_.full().powerRect(); }
    ui::Rect routingArea() const noexcept { return osc_.routingArea(); }
    bool isNativeTuneActive() const noexcept
    {
        return nativeSettingsPublished_ && osc_.topPage() == 0 && osc_.full().hasNativeTune();
    }
    static constexpr ui::Rect tuneArea() noexcept { return OscFullView::tuneAreaAt (0, page::kTabY); }

private:
    nativeui::osc::ViewState nativeState (const IInstanceView& inst) const
    {
        const bool powered = isInstanceOn (inst, sel_);
        return { sel_, nativeRouting_ && powered, preset_, currentDestination (inst, sel_), powered };
    }

    static bool nativeRoot (const UiStack& stack) noexcept { return stack.depth() == 1 && stack.element() == kElementPage; }

    void updateNative (IInstanceView& inst, const UiStack& stack)
    {
        namespace n = nativeui::osc;
        if ((! nativeSelector_ && ! nativeRouting_ && ! osc_.full().hasNativePower()) || ! nativeRoot (stack) || inst.stateGeneration() != preset_)
        {
            hideNative (inst);
            return;
        }
        auto state = nativeState (inst);
        int choice;
        if (nativeSelector_ && inst.takeNativeOsc (state, n::kSource, choice) && inst.stateGeneration() == preset_)
        {
            setSelection (choice + 1);
            if (inPlay_ && tabSelection_ != sel_)
            {
                tabSelection_ = sel_;
                inst.setPlaySelection (0, sel_);
            }
            state = nativeState (inst);
        }
        if (osc_.full().hasNativePower() && inst.takeNativeOsc (state, n::kPower, choice) && (choice != 0) != state.powered
            && inst.stateGeneration() == preset_)
        {
            if (const int idx = powerParamIndex (sel_); idx >= 0)
            {
                osc_.dropGesture();
                inst.writeParam (idx, static_cast<float> (choice));
                state = nativeState (inst);
            }
        }
        if (inst.takeNativeOsc (state, n::kRouting, choice) && state.routable && choice != state.destination
            && inst.stateGeneration() == preset_)
        {
            const int idx = destinationParamIndex (sel_);
            inst.writeParam (idx, static_cast<float> (choice) / n::kLastRoute);
            state.destination = currentDestination (inst, sel_);
        }
        inst.setNativeOscState (state);
        nativePublished_ = true;
    }

    void hideNative (IInstanceView& inst)
    {
        if (nativePublished_)
            inst.setNativeOscState ({});
        nativePublished_ = false;
    }

    bool updateNativeSettings (IInstanceView& inst, UiStack& stack, bool consume)
    {
        namespace n = nativeui::osc;
        const auto view = osc_.nativeSettingsView();
        if (view == n::SettingsHidden || ! nativeRoot (stack) || ! inst.nativeOscSettingsSupported()
            || inst.stateGeneration() != preset_)
        {
            hideNativeSettings (inst);
            return false;
        }

        const bool powered = ! osc_.full().inactive();
        if (nativeSettingsPublished_ && nativeSettingsPowered_ != powered)
            hideNativeSettings (inst);   // expire pending edits across the ON/OFF boundary
        nativeSettingsPowered_ = powered;
        const auto items = osc_.nativeSettingsItems();
        std::array<nativeui::FxCell, n::kSettingsAll> fields {};
        for (int slot = 0; slot < n::kSettingsAll; ++slot)
            if (const auto* item = items[slot])
                fields[slot] = OscUi::nativeSettingsBinding (*item);
        if (view >= n::SettingsWavetable)
            inst.setNativeOscAbcState (osc_.nativeWarpMask(), osc_.nativeSourceLabel());
        inst.setNativeOscSettings (view, fields.data());
        osc_.setNativeSettingsPaint (items);
        nativeSettingsPublished_ = true;
        if (! consume)
            return false;
        for (int slot = 0; slot < n::kSettingsAll; ++slot)
        {
            float value;
            auto* item = items[slot];
            if (item != nullptr && inst.takeNativeOscSetting (slot, fields[slot], value) && inst.stateGeneration() == preset_
                && powered && osc_.applyNativeSetting (*item, inst, stack, value))
            {
                hideNativeSettings (inst);
                return true;   // A layout or stack change expires the pending edits from the former root.
            }
        }
        return false;
    }

    void hideNativeSettings (IInstanceView& inst)
    {
        if (! nativeSettingsPublished_)
            return;
        inst.setNativeOscSettings (nativeui::osc::SettingsHidden, nullptr);
        inst.setNativeOscAbcState (0, "");
        osc_.setNativeSettingsPaint ({});
        nativeSettingsPublished_ = false;
    }

    bool updateNativeDetail (IInstanceView& inst, UiStack& stack, bool consume)
    {
        namespace n = nativeui::oscdetail;
        const auto& state = osc_.prepareNativeDetail (inst.nativeOscDetailSupported() && inst.stateGeneration() == preset_, preset_);
        if (state.frame == n::Hidden && ! nativeDetailPublished_)
            return false;
        inst.setNativeOscDetail (state);
        nativeDetailPublished_ = state.frame != n::Hidden;
        osc_.publishNativeNoteMask (inst, consume && nativeDetailPublished_);
        if (! consume || ! nativeDetailPublished_)
            return false;
        const auto apply = [&] (int field)
        {
            float value;
            if (state.fields[field].target >= 0 && inst.takeNativeOscDetail (state, field, value) && inst.stateGeneration() == preset_
                && osc_.applyNativeDetail (field, value, inst, stack))
            {
                nativePress_ = {-1, -1};
                return true;
            }
            return false;
        };
        // OPTIONS can power off its own source. Consume that first so a queued dial cannot write into a disabled oscillator.
        if (osc_.nativeDetailPower (0) && apply (0))
            return true;
        for (int field = n::kBindings - 1; field >= 0; --field)
        {
            if (apply (field))
                return true;
        }
        return false;
    }

    void hideNativeDetail (IInstanceView& inst)
    {
        if (nativeDetailPublished_)
            inst.setNativeOscDetail (osc_.prepareNativeDetail (false, preset_));
        osc_.publishNativeNoteMask (inst, false);   // the mask item is gone after prepareNativeDetail (false): the cells go
        nativeDetailPublished_ = false;
        nativePress_ = {-1, -1};
    }

    bool inSettingAperture (int x, int y) const noexcept
    {
        const auto& cells = osc_.nativeSettingsCells();
        return std::any_of (cells.begin(), cells.end(), [=] (ui::Rect area) { return area.contains (x, y); });
    }

    bool inActiveAperture (int x, int y) const noexcept
    {
        if (osc_.nativeDetailFieldAt (x, y) >= 0)
            return true;
        if (osc_.topPage() != 0)
            return false;
        if (inPlay_ && expandedCloseRect().contains (x, y))
            return true;
        if (nativeSelector_ && selectorArea().contains (x, y))
            return true;
        if (osc_.full().hasNativePower() && powerArea().contains (x, y))
            return true;
        if (osc_.full().isNativeRouting (x, y))
            return true;
        if (isNativeTuneActive() && tuneArea().contains (x, y))
            return true;
        return inSettingAperture (x, y);
    }

    std::unique_ptr<OscUi> ownedOsc_;
    std::unique_ptr<OscDisplay> ownedDisplay_;
    OscUi& osc_;
    OscDisplay& display_;
    bool inPlay_ = false;
    OscMarks marks_;
    ViewCounters counters_;
    int sel_ = 0, tabSelection_ = 0;
    uint64_t preset_ = 0;
    std::pair<int, int> nativePress_ {-1, -1};
    bool nativePointer_ = false, nativeDetailPublished_ = false, nativeModal_ = false;
    int pressX_ = 0, pressY_ = 0, nativeMenu_ = -1;
    double pressAt_ = 0.0;
    bool nativeSelector_ = false, nativeRouting_ = false, nativePublished_ = false, nativeGroundReady_ = false;
    bool nativeSettingsPublished_ = false, nativeSettingsPowered_ = false;
};

}} // namespace synthyboy_mpc::overlay
