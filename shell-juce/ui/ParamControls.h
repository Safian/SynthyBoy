// SynthyBoy - UI: the reusable controls, bound to a ParamInfo by KEY.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
//   ParamKnob    rotary: drag, fine drag (slide sideways), double-tap = reset (Q3), long-press sheet, purple mod ring
//   ParamSlider  the same on a horizontal track
//   ParamChoice  stepped key: tap opens the option list, < > step, swipe is not needed
//   ParamToggle  two-state stepped key (power buttons, Off / On pills)
//
// All of them: value text with unit from ParamInfo, dimmed with the tooltip "not available in this engine" when the engine lacks the
// caps the key needs, assign-by-touch target (06-modulation.md) when a source chip is armed, right-click / long-press = sheet.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Overlays.h"
#include "ParamHandle.h"
#include "model/GestureModel.h"

namespace synthyboy::ui
{
/** Common base: handle, caps, tooltip, label, assign-state tracking, long-press and the sheet. */
class ParamControl : public juce::Component,
                     public juce::SettableTooltipClient,
                     protected juce::ChangeListener,
                     private juce::Timer
{
public:
    ParamControl (UiContext& context, const std::string& key, const juce::String& labelOverride = {});
    ~ParamControl() override;

    const std::string& key() const noexcept { return handle.key(); }
    ParamHandle& paramHandle() noexcept { return handle; }
    bool isAvailable() const noexcept { return caps.available; }
    const CapState& capState() const noexcept { return caps; }
    const juce::String& label() const noexcept { return labelText; }
    void setLabel (const juce::String& newLabel);

    /** Natural size at the current theme scale; panels lay controls out with it. Always >= the 44 point touch target in both axes. */
    virtual juce::Point<int> preferredSize() const = 0;
    /** True for a control that needs a row of its own (OCT / SEM): the panel layout gives it the full width. */
    virtual bool spansRow() const noexcept { return false; }

    /** Opens the long-press sheet (also on right-click). */
    void showSheet();
    /** Double-click with the preference "Double-click params: value box": the sheet with the cursor in its value box. `onValueBox` (the tests) replaces it. */
    void showValueBox();
    std::function<void()> onValueBox;

    /** A finger that travels beyond the slop is no long-press (a control that is only tapped lets the area it sits in scroll under the finger; the knob and the
        slider have their own drag). */
    void mouseDrag (const juce::MouseEvent&) override; // drag-flag-exempt: it only cancels the long press

protected:
    virtual void valueChanged() { repaint(); }
    virtual void assignStateChanged() { repaint(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    /** Call from mouseDown: arms the long-press timer and clears consumedByMenu. */
    void beginPress();
    void cancelLongPress();
    virtual void longPressed() { showSheet(); }

    bool isAssignTarget() const;
    void paintUnknownKey (juce::Graphics&) const;
    juce::Colour textColour() const { return ctx.theme.colours.text; }

    UiContext& ctx;
    ParamHandle handle;
    CapState caps;
    juce::String labelText;
    bool consumedByMenu = false; // the press opened the sheet (long-press or right-click): no tap action on release

private:
    void timerCallback() override;
};

/** Drag logic shared by knob and slider (docs/ui-design/01 section 3). */
class ParamDragControl : public ParamControl
{
public:
    /** A knob or slider uses the drag: a drag that starts on it never scrolls the area it sits in (setViewportIgnoreDragFlag). */
    ParamDragControl (UiContext& context, const std::string& key, const juce::String& labelOverride = {});
    ~ParamDragControl() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    /** Mouse-less entry points for the tests and for hardware encoders: a full gesture on the control. */
    void nudgeNormalised (float delta);

    /** The count badge of the depth halo is a hit target: tap = the list of the modulators on this control; choosing one arms its source,
        so the next drag on this control sets that modulator's depth (design 06). */
    juce::Rectangle<int> modulatorBadgeBounds() const;
    int modulatorCount() const;
    /** Arms the source of the modulator in `slot` (what the list entry does). False if that slot does not modulate this control. */
    bool selectModulator (int slot);
    bool modulatorListShown() const noexcept { return badgeMenuShown; }
    /** Where the engine plays the control now (normalised), -1 while it is not subscribed to the live bus (not modulated, not on screen, no read-out). */
    float liveNormalisedValue() const noexcept { return liveSubscribed ? liveNormalised : -1.0f; }
    bool liveSubscription() const noexcept { return liveSubscribed; }
    /** One update of the live bus, called by the bus (and by the tests after they polled it). */
    void liveTick();
    /** Re-reads whether the control is modulated and on screen (the JUCE visibility callbacks call it; the tests call it too). */
    void refreshLiveSubscription() { updateLiveSubscription(); }
    /** The value bubble is up (while a drag runs and the preference "Param value tooltips" is on). */
    bool bubbleShown() const noexcept { return bubble != nullptr && bubble->getParentComponent() != nullptr; }
    /** Shift-Alt-click (Appendix G): the polarity of the modulations of this control is switched (all of them to the opposite of the first). False if there is none. */
    bool toggleModulationPolarity();

protected:
    virtual bool isHorizontal() const noexcept { return false; }
    virtual float pixelsForFullRange() const;
    void valueChanged() override;
    void assignStateChanged() override;
    void updateRing();
    void longPressed() override;
    void resized() override;

    /** The bounds of the dial / track the ring is drawn around. */
    virtual juce::Rectangle<int> ringBounds() const = 0;
    virtual ModRing::Shape ringShape() const noexcept = 0;
    void initRing();

    std::unique_ptr<ModRing> ring;

private:
    void showBubble (FineZone zone);
    void hideBubble();

    std::unique_ptr<ValueBubble> bubble;
    juce::Point<float> downPos, lastPos;
    juce::uint32 lastTapMs = 0;
    juce::Point<float> lastTapPos;
    bool pressed = false, dragging = false, assignDrag = false, badgePress = false, badgeMenuShown = false;
    void showModulatorList();
    void updateLiveSubscription();
    void visibilityChanged() override;
    void parentHierarchyChanged() override;
    bool liveSubscribed = false;
    float liveNormalised = -1.0f;
    FineZone zone = FineZone::normal;
};

class ParamKnob final : public ParamDragControl
{
public:
    ParamKnob (UiContext& context, const std::string& key, const juce::String& labelOverride = {});

    juce::Point<int> preferredSize() const override;
    void paint (juce::Graphics&) override;

    /** Compact: no value text (the bubble shows it), label under the dial. Used by the macro row (8 knobs in 390 points). */
    void setCompact (bool shouldBeCompact);
    bool isCompact() const noexcept { return compact; }
    juce::Rectangle<float> dialBounds() const;

protected:
    juce::Rectangle<int> ringBounds() const override { return dialBounds().expanded (ctx.theme.px (4.0f)).toNearestInt(); }
    ModRing::Shape ringShape() const noexcept override { return ModRing::Shape::ring; }

private:
    bool compact = false;
};

class ParamSlider final : public ParamDragControl
{
public:
    ParamSlider (UiContext& context, const std::string& key, const juce::String& labelOverride = {});

    juce::Point<int> preferredSize() const override;
    void paint (juce::Graphics&) override;

protected:
    bool isHorizontal() const noexcept override { return true; }
    float pixelsForFullRange() const override { return juce::jmax (60.0f, static_cast<float> (getWidth()) - ctx.theme.px (24.0f)); }
    juce::Rectangle<int> ringBounds() const override;
    ModRing::Shape ringShape() const noexcept override { return ModRing::Shape::bar; }

private:
    juce::Rectangle<float> trackBounds() const;
};

class ParamChoice final : public ParamControl
{
public:
    ParamChoice (UiContext& context, const std::string& key, const juce::String& labelOverride = {});

    juce::Point<int> preferredSize() const override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    /** Steps the value by +-1 (clamped, unavailable entries are skipped). */
    void step (int direction);
    /** The option list as shown (name, value, enabled): unavailable entries (caps) are disabled. */
    void showOptions();
    /** Grouped option list ("tap a group, tap a mode", design 02 WARP+): the list opens as groups with sub menus. A group with `gap` and no
        items is listed greyed (the design has it, the engine has no mode in it). Without groups the list is flat. */
    void setGroups (std::vector<ChoiceGroup> newGroups) { groups = std::move (newGroups); }
    const std::vector<ChoiceGroup>& choiceGroups() const noexcept { return groups; }
    /** The menu showOptions () shows (the tests inspect it). */
    juce::PopupMenu buildOptionMenu() const;
    /** An action at the end of the option list, behind a separator ("Swap Warps" of the warp lists, 13-new-keys.md 4.22); the menu result
        is not a value. runExtraAction () is what the menu item does (the tests call it). */
    void setExtraAction (juce::String label, std::function<void()> action);
    bool hasExtraAction() const noexcept { return static_cast<bool> (extraAction); }
    void runExtraAction() const { if (extraAction) extraAction(); }
    static constexpr int kExtraActionId = 100000;
    /** A panel can switch single entries off (shown greyed, skipped by the arrows): the Ratio SRC of an oscillator cannot be the oscillator itself or close a loop
        (GROUPB WP-05). The filter gets the choice value and returns true if the entry may be chosen. */
    void setEntryFilter (std::function<bool (int)> filter) { entryFilter = std::move (filter); }
    bool entryAllowed (int value) const { return ! entryFilter || entryFilter (value); }

private:
    std::function<bool (int)> entryFilter;
    std::vector<ChoiceGroup> groups;
    juce::String extraLabel;
    std::function<void()> extraAction;
    juce::Rectangle<int> arrowArea (bool left) const;
    bool pressed = false;
    juce::Point<float> downPos;
};

class ParamToggle final : public ParamControl
{
public:
    ParamToggle (UiContext& context, const std::string& key, const juce::String& labelOverride = {});

    juce::Point<int> preferredSize() const override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isOn() const { return handle.valid() && handle.plain() >= 0.5f; }
    void toggle();

private:
    bool pressed = false;
};

/** Makes the right control for a key: knob / toggle / choice by ParamInfo (slider on request). */
std::unique_ptr<ParamControl> makeControl (UiContext& context, const std::string& key, const juce::String& labelOverride = {});
} // namespace synthyboy::ui
