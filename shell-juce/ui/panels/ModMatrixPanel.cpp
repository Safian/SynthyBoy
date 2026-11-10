// SynthyBoy - UI panel: Modulation Matrix (docs/ui-design/06-modulation.md, page `mod`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "../Panel.h"
#include "synthyboy/shell_features.h"
#include "ModLogic.h"
#include "ModMatrixReorder.h"

#include <algorithm>
#include <array>
#include <map>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace synthyboy::ui
{
namespace
{
/** Compact slot row inside the matrix list. */
class ModSlotRow final : public juce::Component
{
public:
    ModSlotRow (UiContext& context, int slotIndex,
                const std::function<void()>& onSlotChanged,
                const std::function<void(int)>& onSelectCurveSlot,
                const std::function<int (int, int)>& onMoveSlot)
        : ctx (context), slot (slotIndex),
          notifyChanged (onSlotChanged), selectCurveSlot (onSelectCurveSlot), moveSlotCb (onMoveSlot)
    {
        slotLabel.setText (juce::String::formatted ("#%02d", slot + 1), juce::dontSendNotification);
        slotLabel.setInterceptsMouseClicks (false, false); // the label is the handle: the row takes its presses
        slotLabel.setTooltip ("Drag up / down to reorder, tap for the row menu");
        slotLabel.setFont (ctx.theme.font (ctx.theme.px (12.0f), true));
        slotLabel.setColour (juce::Label::textColourId, ctx.theme.colours.textDim);
        addAndMakeVisible (slotLabel);

        bypassButton.setButtonText ("Byp");
        bypassButton.setClickingTogglesState (true);
        bypassButton.setTooltip ("Bypass this modulation slot");
        bypassButton.onClick = [this] { toggleBypass(); };
        addAndMakeVisible (bypassButton);

        sourceButton.setTooltip ("Select modulation source");
        sourceButton.onClick = [this] { showSourceMenu(); };
        addAndMakeVisible (sourceButton);

        arrowLabel.setText ("->", juce::dontSendNotification);
        arrowLabel.setJustificationType (juce::Justification::centred);
        arrowLabel.setColour (juce::Label::textColourId, ctx.theme.colours.textDim);
        addAndMakeVisible (arrowLabel);

        destButton.setTooltip ("Select modulation destination");
        destButton.onClick = [this] { showDestMenu(); };
        addAndMakeVisible (destButton);

        amountSlider.setRange (-100.0, 100.0, 0.1);
        amountSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        amountSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, ctx.theme.ipx (54.0f), ctx.theme.ipx (20.0f));
        amountSlider.setTextValueSuffix (" %");
        amountSlider.setTooltip ("Modulation Amount (-100% .. +100%)");
        amountSlider.onValueChange = [this] {
            if (! updating)
            {
                const auto p = modSlotParams (slot);
                if (p.amount >= 0)
                {
                    ctx.assign.matrix().set (p.amount, static_cast<float> (amountSlider.getValue()));
                    if (notifyChanged) notifyChanged();
                }
            }
        };
        addAndMakeVisible (amountSlider);

        bipolarButton.setButtonText ("UNI");
        bipolarButton.setClickingTogglesState (true);
        bipolarButton.setTooltip ("Toggle Bipolar / Unipolar Modulation");
        bipolarButton.onClick = [this] { toggleBipolar(); };
        addAndMakeVisible (bipolarButton);

        stereoButton.setButtonText ("MONO");
        stereoButton.setClickingTogglesState (true);
        stereoButton.setTooltip ("Toggle Stereo Invert Modulation (L = +amt, R = -amt)");
        stereoButton.onClick = [this] { toggleStereo(); };
        addAndMakeVisible (stereoButton);

        curveButton.setButtonText ("Crv");
        curveButton.setTooltip ("Inspect & edit remap curve in Slot Curve Inspector");
        curveButton.onClick = [this] {
            if (selectCurveSlot) selectCurveSlot (slot);
        };
        addAndMakeVisible (curveButton);

        loopBadge.setText ("LOOP!", juce::dontSendNotification);
        loopBadge.setJustificationType (juce::Justification::centred);
        loopBadge.setColour (juce::Label::backgroundColourId, ctx.theme.colours.warn);
        loopBadge.setColour (juce::Label::textColourId, ctx.theme.colours.textOnAccent);
        loopBadge.setFont (ctx.theme.font (ctx.theme.px (10.0f), true));
        loopBadge.setVisible (false);
        addAndMakeVisible (loopBadge);

        removeButton.setButtonText ("x");
        removeButton.setTooltip ("Remove this modulation routing");
        removeButton.onClick = [this] { clearThisSlot(); };
        addAndMakeVisible (removeButton);

        refresh();
    }

    void refresh()
    {
        updating = true;
        const auto snap = readSlot (ctx.assign.matrix(), slot);
        active = snap.inUse();
        bypassed = snap.bypass;

        // Bypass button appearance
        bypassButton.setToggleState (bypassed, juce::dontSendNotification);
        bypassButton.setButtonText (bypassed ? "Off" : "On");

        // Source display
        if (snap.source >= 1 && snap.source <= static_cast<int> (kNumModSources))
            sourceButton.setButtonText (modSourceTable()[snap.source - 1].name);
        else
            sourceButton.setButtonText ("None");

        // Destination display
        if (snap.destination > 0)
        {
            const int pIndex = modDestinationParamIndex (static_cast<size_t> (snap.destination));
            if (pIndex >= 0 && static_cast<size_t> (pIndex) < paramCount())
                destButton.setButtonText (paramTable()[pIndex].name);
            else
                destButton.setButtonText ("Unknown");
        }
        else
        {
            destButton.setButtonText ("None");
        }

        // Amount & Polarity & Stereo
        amountSlider.setValue (snap.amount, juce::dontSendNotification);
        bipolarButton.setToggleState (snap.bipolar, juce::dontSendNotification);
        bipolarButton.setButtonText (snap.bipolar ? "BIP" : "UNI");

        stereoButton.setToggleState (snap.stereo, juce::dontSendNotification);
        stereoButton.setButtonText (snap.stereo ? "STER" : "MONO");

        char crvBuf[16];
        std::snprintf (crvBuf, sizeof crvBuf, "C:%+.1f", static_cast<double> (snap.power));
        // a star marks a drawn slot curve (content.modCurves, G2)
        const auto& drawn = ctx.host.content().modCurves;
        const bool hasCurve = std::any_of (drawn.begin(), drawn.end(), [this] (const ContentModCurve& m) { return m.slot == slot + 1 && m.points.size() >= 2; });
        // ... and a RISE / FALL / DELAY of the slot (the keys modNN.rise / fall / delay / delay_beats, REMAINDER item 1)
        const auto sp = modSlotParams (slot);
        const auto above = [this] (int index) { return index >= 0 && ctx.assign.matrix().get (index) > 0.0f; };
        const bool hasDrawn = hasCurve || above (sp.rise) || above (sp.fall) || above (sp.delay) || above (sp.delayBeats);
        curveButton.setButtonText (juce::String (crvBuf) + (hasDrawn ? "*" : ""));

        // Feedback loop check
        const auto loop = modDetectFeedbackLoop (ctx.assign.matrix(), slot);
        loopBadge.setVisible (loop.isLoop);
        if (loop.isLoop)
            loopBadge.setTooltip (juce::String (loop.reason));

        // Dim bypassed row to 45% opacity
        setAlpha (bypassed ? 0.45f : 1.0f);

        updating = false;
        repaint();
    }

    bool isActive() const noexcept { return active; }

    int handleWidth() const { return juce::jmax (ctx.theme.touch(), ctx.theme.ipx (44.0f)); }
    juce::Rectangle<int> handleBounds() const { return { 0, 0, handleWidth() + ctx.theme.ipx (2.0f), getHeight() }; }
    bool menuShown() const noexcept { return menuWasShown; }

    // the handle: a drag moves the content of the row past its neighbours, a tap opens the row menu (Move up / Move down)
    void mouseDown (const juce::MouseEvent& e) override
    {
        handleDown = handleBounds().contains (e.getPosition());
        handleMoved = false;
        dragSlot = slot;
        anchorY = e.position.y;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! handleDown || ! moveSlotCb)
            return;
        const float rowH = static_cast<float> (juce::jmax (1, getHeight()));
        const float dy = e.position.y - anchorY;
        if (std::abs (dy) >= rowH * 0.6f)
        {
            const int dir = dy > 0.0f ? 1 : -1;
            const int moved = moveSlotCb (dragSlot, dir);
            if (moved >= 0)
            {
                dragSlot = moved; // the finger keeps the content it picked up
                handleMoved = true;
            }
            anchorY += static_cast<float> (dir) * rowH;
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const bool tap = handleDown && ! handleMoved && e.getDistanceFromDragStart() < ctx.theme.ipx (8.0f);
        handleDown = false;
        if (tap)
            showRowMenu();
    }

    void showRowMenu()
    {
        menuWasShown = true;
        juce::PopupMenu menu;
        menu.addItem (1, "Move up");
        menu.addItem (2, "Move down");
        menu.addSeparator();
        menu.addItem (3, "Remove");
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [safe = juce::Component::SafePointer<ModSlotRow> (this)] (int result)
                            {
                                if (safe == nullptr || result <= 0)
                                    return;
                                if (result == 3)
                                    safe->clearThisSlot();
                                else if (safe->moveSlotCb)
                                    safe->moveSlotCb (safe->slot, result == 1 ? -1 : +1);
                            });
    }

    bool matchesSearch (const juce::String& text) const
    {
        if (text.isEmpty()) return true;
        return sourceButton.getButtonText().containsIgnoreCase (text)
            || destButton.getButtonText().containsIgnoreCase (text);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (ctx.theme.ipx (2.0f), ctx.theme.ipx (2.0f));
        slotLabel.setBounds (r.removeFromLeft (handleWidth()));
        bypassButton.setBounds (r.removeFromLeft (ctx.theme.ipx (36.0f)));
        r.removeFromLeft (ctx.theme.ipx (4.0f));

        const int btnW = juce::jmax (ctx.theme.ipx (80.0f), (r.getWidth() - ctx.theme.ipx (240.0f)) / 2);
        sourceButton.setBounds (r.removeFromLeft (btnW));
        arrowLabel.setBounds (r.removeFromLeft (ctx.theme.ipx (20.0f)));
        destButton.setBounds (r.removeFromLeft (btnW));
        r.removeFromLeft (ctx.theme.ipx (4.0f));

        removeButton.setBounds (r.removeFromRight (ctx.theme.ipx (26.0f)));
        r.removeFromRight (ctx.theme.ipx (4.0f));

        if (loopBadge.isVisible())
        {
            loopBadge.setBounds (r.removeFromRight (ctx.theme.ipx (44.0f)));
            r.removeFromRight (ctx.theme.ipx (4.0f));
        }

        curveButton.setBounds (r.removeFromRight (ctx.theme.ipx (48.0f)));
        r.removeFromRight (ctx.theme.ipx (4.0f));
        stereoButton.setBounds (r.removeFromRight (ctx.theme.ipx (44.0f)));
        r.removeFromRight (ctx.theme.ipx (4.0f));
        bipolarButton.setBounds (r.removeFromRight (ctx.theme.ipx (40.0f)));
        r.removeFromRight (ctx.theme.ipx (4.0f));

        amountSlider.setBounds (r);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = ctx.theme;
        g.setColour (active ? t.colours.panelRaised : t.colours.panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), t.px (4.0f));
        g.setColour (t.colours.outline);
        g.drawRoundedRectangle (getLocalBounds().toFloat(), t.px (4.0f), 1.0f);
    }

private:
    void toggleBypass()
    {
        const auto p = modSlotParams (slot);
        if (p.bypass >= 0)
        {
            const bool newByp = ! (ctx.assign.matrix().get (p.bypass) >= 0.5f);
            ctx.assign.matrix().set (p.bypass, newByp ? 1.0f : 0.0f);
            refresh();
            if (notifyChanged) notifyChanged();
        }
    }

    void toggleBipolar()
    {
        const auto p = modSlotParams (slot);
        if (p.bipolar >= 0)
        {
            const bool newBip = ! (ctx.assign.matrix().get (p.bipolar) >= 0.5f);
            ctx.assign.matrix().set (p.bipolar, newBip ? 1.0f : 0.0f);
            refresh();
            if (notifyChanged) notifyChanged();
        }
    }

    void toggleStereo()
    {
        const auto p = modSlotParams (slot);
        if (p.stereo >= 0)
        {
            const bool newSt = ! (ctx.assign.matrix().get (p.stereo) >= 0.5f);
            ctx.assign.matrix().set (p.stereo, newSt ? 1.0f : 0.0f);
            refresh();
            if (notifyChanged) notifyChanged();
        }
    }

    void clearThisSlot()
    {
        clearSlot (ctx.assign.matrix(), slot);
        ctx.assign.refresh();
        refresh();
        if (notifyChanged) notifyChanged();
    }

    void showSourceMenu()
    {
        juce::PopupMenu menu;
        menu.addItem (1000, "None", true, false);
        menu.addSeparator();

        juce::PopupMenu envMenu;
        for (int i = 1; i <= 6; ++i)
            envMenu.addItem (i, modSourceTable()[i - 1].name);
        menu.addSubMenu ("Envelopes", envMenu);

        juce::PopupMenu lfoMenu;
        for (int i = 7; i <= 14; ++i)
            lfoMenu.addItem (i, modSourceTable()[i - 1].name);
        menu.addSubMenu ("LFOs", lfoMenu);

        juce::PopupMenu rndMenu;
        for (int i = 15; i <= 18; ++i)
            rndMenu.addItem (i, modSourceTable()[i - 1].name);
        menu.addSubMenu ("Random LFOs", rndMenu);

        juce::PopupMenu macroMenu;
        for (int i = 19; i <= 22; ++i)
            macroMenu.addItem (i, modSourceTable()[i - 1].name);
        for (int i = 29; i <= 32; ++i)
            macroMenu.addItem (i, modSourceTable()[i - 1].name);
        menu.addSubMenu ("Macros", macroMenu);

        juce::PopupMenu midiMenu;
        menu.addItem (23, "Velocity");
        menu.addItem (24, "Note");
        menu.addItem (25, "Aftertouch");
        menu.addItem (26, "Pitch Wheel");
        menu.addItem (27, "Mod Wheel");
        menu.addItem (28, "Random per Note");
        menu.addItem (33, "LFO 9");
        menu.addItem (34, "LFO 10");
        menu.addItem (35, "Lift");
        menu.addItem (36, "Slide");
        menu.addItem (37, "Oct Note");
        menu.addItem (38, "Poly Aftertouch");
        menu.addItem (39, "Stereo");

        // the sources of the voice control and of GROUPB WP-06 (13-new-keys.md 4.34): the outputs of the sound sources and the sources of the note counter
        const int sourceKey = findParamIndex (hashKey ("mod01.source"));
        const auto addGroup = [&] (const char* name, int first, int last)
        {
            juce::PopupMenu group;
            for (int i = first; i <= last; ++i)
                group.addItem (i, modSourceTable()[i - 1].name, capStateForValue (sourceKey, static_cast<float> (i), ctx.host.engineCaps()).available);
            menu.addSubMenu (name, group);
        };
        addGroup ("Voice Control", 40, 42);
        addGroup ("Sound Sources", 43, 49);
        addGroup ("Note & Voice", 50, 57);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sourceButton),
                            [this] (int result) {
            if (result == 0) return;
            const auto p = modSlotParams (slot);
            if (p.source >= 0)
            {
                const int choice = (result == 1000) ? 0 : result;
                ctx.assign.matrix().set (p.source, static_cast<float> (choice));
                ctx.assign.refresh();
                refresh();
                if (notifyChanged) notifyChanged();
            }
        });
    }

    void showDestMenu()
    {
        juce::PopupMenu menu;
        menu.addItem (2000, "None", true, false);
        menu.addSeparator();

        const size_t totalDest = modDestinationCount();
        std::map<std::string, juce::PopupMenu> subMenus;
        // the amounts of the 64 slots (G1) are one submenu "Mod Amounts" in eight groups of eight; a slot that would close a loop is greyed out
        std::array<juce::PopupMenu, kNumModSlots / 8> amountGroups;

        for (size_t d = 1; d <= totalDest; ++d)
        {
            const int pIndex = modDestinationParamIndex (d);
            if (pIndex >= 0 && static_cast<size_t> (pIndex) < paramCount())
            {
                const auto& info = paramTable()[pIndex];
                const int amountSlot = modAmountSlotOfChoice (static_cast<int> (d));
                if (amountSlot >= 0)
                {
                    const bool loops = modAmountWouldLoop (ctx.assign.matrix(), slot, static_cast<int> (d));
                    amountGroups[static_cast<size_t> (amountSlot / 8)].addItem (static_cast<int> (d), juce::String (info.name) + (loops ? " (loop)" : ""), ! loops,
                                                                                 static_cast<int> (std::lround (ctx.assign.matrix().get (modSlotParams (slot).destination))) == static_cast<int> (d));
                    continue;
                }
                std::string group = info.group;
                const size_t cut = group.find (" / ");
                if (cut != std::string::npos)
                    group = group.substr (0, cut);
                subMenus[group].addItem (static_cast<int> (d), info.name);
            }
        }

        {
            juce::PopupMenu amounts;
            for (size_t g = 0; g < amountGroups.size(); ++g)
                amounts.addSubMenu (juce::String::formatted ("Slots %d-%d", static_cast<int> (g) * 8 + 1, static_cast<int> (g) * 8 + 8), amountGroups[g]);
            subMenus["Mod Amounts"] = std::move (amounts);
        }

        for (auto& pair : subMenus)
            menu.addSubMenu (pair.first, pair.second);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&destButton),
                            [this] (int result) {
            if (result == 0) return;
            const auto p = modSlotParams (slot);
            if (p.destination >= 0)
            {
                const int choice = (result == 2000) ? 0 : result;
                ctx.assign.matrix().set (p.destination, static_cast<float> (choice));
                if (choice > 0 && p.bipolar >= 0)
                {
                    const int pIdx = modDestinationParamIndex (static_cast<size_t> (choice));
                    if (pIdx >= 0 && static_cast<size_t> (pIdx) < paramCount())
                    {
                        const bool bip = AssignFlow::defaultBipolar (paramTable()[pIdx]);
                        ctx.assign.matrix().set (p.bipolar, bip ? 1.0f : 0.0f);
                    }
                }
                ctx.assign.refresh();
                refresh();
                if (notifyChanged) notifyChanged();
            }
        });
    }

    UiContext& ctx;
    int slot = 0;
    std::function<void()> notifyChanged;
    std::function<void(int)> selectCurveSlot;
    std::function<int (int, int)> moveSlotCb;
    bool handleDown = false, handleMoved = false, menuWasShown = false;
    int dragSlot = 0;
    float anchorY = 0.0f;
    bool active = false;
    bool bypassed = false;
    bool updating = false;

    juce::Label slotLabel;
    juce::TextButton bypassButton;
    juce::TextButton sourceButton;
    juce::Label arrowLabel;
    juce::TextButton destButton;
    juce::Slider amountSlider;
    juce::TextButton bipolarButton;
    juce::TextButton stereoButton;
    juce::TextButton curveButton;
    juce::Label loopBadge;
    juce::TextButton removeButton;
};

/** Container inside the viewport. */
class ModSlotContainer final : public juce::Component
{
public:
    std::vector<std::unique_ptr<ModSlotRow>> rows;
};
} // namespace

class ModMatrixPanel final : public Panel,
                             public SlotReorder,
                             private juce::Timer
{
public:
    explicit ModMatrixPanel (UiContext& context)
        : Panel (context)
    {
    }

    ~ModMatrixPanel() override
    {
        stopTimer();
    }

    juce::String title() const override { return "Modulation Matrix"; }

    std::vector<std::string> keys() const override
    {
        std::vector<std::string> list;
        list.reserve (kNumModSlots * 11);

        // 4 primary controls for slot 01
        list.push_back ("mod01.amount");
        list.push_back ("mod01.power");
        list.push_back ("mod01.source");
        list.push_back ("mod01.destination");

        // More controls for slot 01
        list.push_back ("mod01.bipolar");
        list.push_back ("mod01.stereo");
        list.push_back ("mod01.bypass");
        list.push_back ("mod01.aux_source");
        list.push_back ("mod01.aux_inv");
        list.push_back ("mod01.aux_power");
        list.push_back ("mod01.aux_output");

        // Remaining slots 02..64
        for (int s = 2; s <= kNumModSlots; ++s)
        {
            char buf[32];
            const auto add = [&] (const char* field) {
                std::snprintf (buf, sizeof buf, "mod%02d.%s", s, field);
                list.emplace_back (buf);
            };
            add ("source");
            add ("destination");
            add ("amount");
            add ("power");
            add ("bipolar");
            add ("stereo");
            add ("bypass");
            add ("aux_source");
            add ("aux_inv");
            add ("aux_power");
            add ("aux_output");
        }
        return list;
    }

    void build() override
    {
        const auto all = keys();
        for (size_t i = 0; i < all.size(); ++i)
        {
            if (i < 4)
                addControl (all[i]);
            else
                addMoreControl (all[i]);
        }

        filterCombo.addItem ("Active Slots", 1);
        filterCombo.addItem ("All 64 Slots", 2);
        filterCombo.setSelectedId (1, juce::dontSendNotification);
        filterCombo.onChange = [this] { layoutSlotRows(); };
        addAndMakeVisible (filterCombo);

        searchEditor.setTextToShowWhenEmpty ("Search params...", ctx.theme.colours.textDim);
        searchEditor.onTextChange = [this] { layoutSlotRows(); };
        addAndMakeVisible (searchEditor);

        opsButton.setButtonText ("Ops Menu");
        opsButton.setTooltip ("Matrix batch operations (Sort, Lock, Vibrato, Bake, Init)");
        opsButton.onClick = [this] { showOpsMenu(); };
        addAndMakeVisible (opsButton);

        addSlotButton.setButtonText ("+ Add Slot");
        addSlotButton.setTooltip ("Claim next free modulation matrix slot");
        addSlotButton.onClick = [this] { addNewSlot(); };
        addAndMakeVisible (addSlotButton);

        statusLabel.setColour (juce::Label::textColourId, ctx.theme.colours.textDim);
        statusLabel.setFont (ctx.theme.labelFont());
        addAndMakeVisible (statusLabel);

        emptyNotice.setText ("No active modulation slots.\nTap '+ Add Slot' or tap a source chip above and drag any knob.",
                             juce::dontSendNotification);
        emptyNotice.setJustificationType (juce::Justification::centred);
        emptyNotice.setColour (juce::Label::textColourId, ctx.theme.colours.textDim);
        emptyNotice.setFont (ctx.theme.font (ctx.theme.px (13.0f)));
        container.addAndMakeVisible (emptyNotice);

        for (int i = 0; i < kNumModSlots; ++i)
        {
            auto row = std::make_unique<ModSlotRow> (ctx, i,
                [this] { updateStatus(); },
                [] (int) {},
                [this] (int from, int direction) { return moveSlot (from, direction); });
            container.addAndMakeVisible (*row);
            container.rows.push_back (std::move (row));
        }

        viewport.setViewedComponent (&container, false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::nonHover);
        addAndMakeVisible (viewport);

        updateStatus();
        startTimer (150); // periodic UI sync for host automation
    }

    int preferredHeight (int width) const override
    {
        return ctx.theme.ipx (width < 600 ? 520.0f : 580.0f);
    }

    // ---- SlotReorder ----
    int moveSlot (int slot, int direction) override
    {
        if (slot < 0 || slot >= kNumModSlots || direction == 0)
            return -1;
        const int step = direction < 0 ? -1 : 1;
        int other = slot + step;
        while (other >= 0 && other < kNumModSlots && ! container.rows[static_cast<size_t> (other)]->isVisible())
            other += step;
        if (other < 0 || other >= kNumModSlots || ! swapModSlots (ctx.assign.matrix(), slot, other))
            return -1;
        ctx.assign.refresh();
        for (auto& r : container.rows)
            r->refresh();
        layoutSlotRows();
        return other;
    }
    juce::Component* slotRowComponent (int slot) override
    {
        return slot >= 0 && slot < kNumModSlots ? container.rows[static_cast<size_t> (slot)].get() : nullptr;
    }
    juce::Rectangle<int> slotHandleBounds (int slot) override
    {
        return slot >= 0 && slot < kNumModSlots ? container.rows[static_cast<size_t> (slot)]->handleBounds() : juce::Rectangle<int>();
    }
    bool slotRowShown (int slot) override
    {
        return slot >= 0 && slot < kNumModSlots && container.rows[static_cast<size_t> (slot)]->isVisible();
    }
    void refreshRows() override
    {
        for (auto& r : container.rows)
            r->refresh();
        layoutSlotRows();
        updateStatus();
    }
    bool rowMenuWasShown() const override
    {
        for (const auto& r : container.rows)
            if (r->menuShown())
                return true;
        return false;
    }

    void resized() override
    {
        auto area = contentArea();
        const int h = ctx.theme.touch();
        const int gap = ctx.theme.gap();

        auto topBar = area.removeFromTop (h);
        opsButton.setBounds (topBar.removeFromRight (ctx.theme.ipx (84.0f)));
        topBar.removeFromRight (gap);
        addSlotButton.setBounds (topBar.removeFromRight (ctx.theme.ipx (84.0f)));
        topBar.removeFromRight (gap);
        filterCombo.setBounds (topBar.removeFromLeft (ctx.theme.ipx (110.0f)));
        topBar.removeFromLeft (gap);
        searchEditor.setBounds (topBar.removeFromLeft (ctx.theme.ipx (120.0f)));
        topBar.removeFromLeft (gap);
        statusLabel.setBounds (topBar);

        area.removeFromTop (gap);
        viewport.setBounds (area);
        layoutSlotRows();
    }

private:
    void timerCallback() override
    {
        const int used = modCountActiveSlots (ctx.assign.matrix());
        if (used != lastActiveCount)
        {
            lastActiveCount = used;
            for (auto& r : container.rows)
                r->refresh();
            layoutSlotRows();
            updateStatus();
        }
    }

    void updateStatus()
    {
        const int used = modCountActiveSlots (ctx.assign.matrix());
        lastActiveCount = used;
        statusLabel.setText (juce::String::formatted ("%d / 64 Active", used) + (ctx.host.isModuleLocked (kMatrixLock) ? juce::String ("  (locked)") : juce::String()),
                             juce::dontSendNotification);
    }

    void layoutSlotRows()
    {
        const bool showAll = filterCombo.getSelectedId() == 2;
        const juce::String query = searchEditor.getText().trim();
        const int rowH = ctx.theme.touch();
        const int gap = ctx.theme.ipx (3.0f);
        const int w = viewport.getWidth() - viewport.getScrollBarThickness();

        int y = 0;
        int visibleCount = 0;

        for (auto& r : container.rows)
        {
            const bool visible = (showAll || r->isActive()) && r->matchesSearch (query);
            r->setVisible (visible);
            if (visible)
            {
                r->setBounds (0, y, w, rowH);
                y += rowH + gap;
                ++visibleCount;
            }
        }

        emptyNotice.setVisible (visibleCount == 0);
        if (visibleCount == 0)
        {
            emptyNotice.setBounds (0, 0, w, ctx.theme.ipx (100.0f));
            y = ctx.theme.ipx (100.0f);
        }

        container.setSize (w, y);
    }

    void addNewSlot()
    {
        for (int i = 0; i < kNumModSlots; ++i)
        {
            auto s = readSlot (ctx.assign.matrix(), i);
            if (! s.inUse())
            {
                const int armed = ctx.assign.flow().armedSource();
                s.source = (armed > 0) ? armed : 7; // LFO 1
                s.destination = 1; // first modulatable parameter
                s.amount = 50.0f;
                s.bipolar = false;
                s.stereo = false;
                s.bypass = false;
                writeSlot (ctx.assign.matrix(), i, s);
                ctx.assign.refresh();
                container.rows[static_cast<size_t> (i)]->refresh();
                layoutSlotRows();
                updateStatus();
                viewport.setViewPosition (0, container.rows[static_cast<size_t> (i)]->getY());
                return;
            }
        }
        ctx.assign.setMessage ("Mod Matrix full (64/64 slots used)");
    }

    void showOpsMenu()
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Sort by Source (ascending)");
        menu.addItem (2, "Sort by Destination (ascending)");
        menu.addItem (3, "Lock Matrix (preserve across presets)", true, ctx.host.isModuleLocked (kMatrixLock));
        menu.addSeparator();
        menu.addItem (4, "Create Vibrato (Unused LFO -> Pitch via Wheel)");
        menu.addItem (5, "Create Velo -> Amp Assignment");
        menu.addSeparator();
        menu.addItem (6, "Apply and Delete Macros ('Bake Macros')");
        menu.addItem (7, "Swap Macros...");
        menu.addSeparator();
        menu.addItem (8, "Init Modulations (Clear All Slots)");

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&opsButton),
                            [this] (int result) {
            auto& m = ctx.assign.matrix();
            if (result == 1)
            {
                modSortSlotsBySource (m);
            }
            else if (result == 2)
            {
                modSortSlotsByDestination (m);
            }
            else if (result == 3)
            {
                // Lock Matrix (S p.214, GROUPA2 WP-02): the host keeps the 64 slots and their curves when a preset is loaded
                ctx.host.setModuleLocked (kMatrixLock, ! ctx.host.isModuleLocked (kMatrixLock));
                updateStatus();
            }
            else if (result == 4)
            {
                modCreateVibrato (m, [&] (int p, float v) { m.set (p, v); });
            }
            else if (result == 5)
            {
                modCreateVeloAmp (m);
            }
            else if (result == 6)
            {
                modBakeMacros (m,
                    [&] (int p) { return m.get (p); },
                    [&] (int p, float v) { m.set (p, v); });
            }
            else if (result == 7)
            {
                showSwapMacroDialog();
                return;
            }
            else if (result == 8)
            {
                modInitAllSlots (m);
            }

            if (result != 0)
            {
                ctx.assign.refresh();
                for (auto& r : container.rows)
                    r->refresh();
                layoutSlotRows();
                updateStatus();
            }
        });
    }

    void showSwapMacroDialog()
    {
        juce::PopupMenu menu;
        for (int a = 1; a <= 8; ++a)
        {
            juce::PopupMenu sub;
            for (int b = 1; b <= 8; ++b)
            {
                if (a != b)
                    sub.addItem (a * 10 + b, juce::String::formatted ("Swap with Macro %d", b));
            }
            menu.addSubMenu (juce::String::formatted ("Macro %d...", a), sub);
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&opsButton),
                            [this] (int res) {
            if (res > 0)
            {
                const int a = res / 10;
                const int b = res % 10;
                auto& m = ctx.assign.matrix();
                modSwapMacros (m, a, b,
                    [&] (int p) { return m.get (p); },
                    [&] (int p, float v) { m.set (p, v); });
                ctx.assign.refresh();
                for (auto& r : container.rows)
                    r->refresh();
                layoutSlotRows();
                updateStatus();
            }
        });
    }

    juce::ComboBox filterCombo;
    juce::TextEditor searchEditor;
    juce::TextButton opsButton;
    juce::TextButton addSlotButton;
    juce::Label statusLabel;
    juce::Label emptyNotice;

    juce::Viewport viewport;
    ModSlotContainer container;

    int lastActiveCount = -1;
};

SYNTHYBOY_REGISTER_PANEL (ModMatrixPanel, Page::mod, 10)
} // namespace synthyboy::ui
