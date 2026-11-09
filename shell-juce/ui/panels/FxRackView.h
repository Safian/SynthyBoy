// SynthyBoy - UI: the racks of the effect chain as strips of module cards (docs/ui-design/08-effects.md, 18-FX-INSTANCES.md section 2.8).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Main, Bus 1, Bus 2 (and the band racks of a splitter while they hold a module) are strips of cards: the 16 modules of instance 0 and the modules of the
// instance pool, mixed in the order the engine processes them. Gestures: tap selects (a pool module opens the panel of its type below, bound to the slot),
// tap on the power dot or double tap toggles the module, hold still on a card and drag = reorder and move between racks (drop line), release after the hold
// without moving = the card menu (bypass, duplicate, remove, module sheet), the "+" tile at the end of a main rack opens the type picker and claims the
// first free slot. Visible alternatives for every gesture: < Move Left / Move Right >, ON / OFF, Bypass All, Duplicate, Remove, + Add.
// The edits are the functions of core/fx_rack.h applied through FxRackOps.h; this class only draws, hit-tests and forwards.
#pragma once

#include <functional>

#include "../FxRackOps.h"
#include "../GestureKit.h"
#include "../ModuleSheet.h"
#include "FxCommon.h"
#include "../LibraryMenu.h"
#include "synthyboy/library_items.h"

namespace synthyboy::ui
{
class FxRackView final : public FxVisualBase
{
public:
    explicit FxRackView (UiContext& context)
        : FxVisualBase (context, "Racks (hold + drag = reorder, + = add a module)"),
          moveLeftButton ("< Move Left"),
          moveRightButton ("Move Right >"),
          powerButton ("ON"),
          bypassAllButton ("Bypass All"),
          duplicateButton ("Duplicate"),
          removeButton ("Remove"),
          addButton ("+ Add"),
          presetsButton ("Presets"),
          busButton ("Bus ...")
    {
        initRackPresets();
        // everything that decides the racks: the order, the power and the rack of the 16 modules, type / power / rack / order of the 16 slots
        const auto watch = [this] (const std::string& key)
        {
            watchers.emplace_back (std::make_unique<ParamHandle> (ctx, key));
            watchers.back()->onChange = [this] { refresh(); };
        };
        for (int i = 1; i <= 16; ++i)
            watch ("fx.order." + std::to_string (i));
        for (int i = 0; i < 16; ++i)
        {
            watch (kFxOnKeys[i]);
            watch (kFxRackKeys[i]);
        }
        for (int s = 0; s < kNumFxInstances; ++s)
            for (const int index : { fxInstanceKeys (s).type, fxInstanceKeys (s).on, fxInstanceKeys (s).rack, fxInstanceKeys (s).order })
                watch (paramTable()[index].key);

        moveLeftButton.onClick = [this] { moveSelected (-1); };
        moveRightButton.onClick = [this] { moveSelected (1); };
        powerButton.onClick = [this]
        {
            if (juce::ModifierKeys::currentModifiers.isAltDown())
                bypassAll();
            else if (hasSelection)
                togglePower (selection);
        };
        bypassAllButton.onClick = [this] { bypassAll(); };
        duplicateButton.onClick = [this] { if (hasSelection) duplicate (selection); };
        removeButton.onClick = [this] { if (hasSelection && selection.pool) removeSlot (selection.index); };
        addButton.onClick = [this] { showTypePicker (hasSelection ? juce::jmin (model.rackOf (selection), 2) : 0); };
        busButton.onClick = [this] { showBusMenu (hasSelection ? juce::jmin (model.rackOf (selection), 2) : 0); };
        press.onLongPress = [this]
        {
            if (dragItemValid)
            {
                reorderArmed = true; // held still on a card: now it can be dragged to another place
                repaint();
            }
        };

        for (auto* b : { &moveLeftButton, &moveRightButton, &powerButton, &bypassAllButton, &duplicateButton, &removeButton, &addButton, &presetsButton, &busButton })
            addAndMakeVisible (*b);
        refresh();
    }

    // ---- the model --------------------------------------------------------------------------------------------------------------------
    const FxRacks& racks() const noexcept { return model.racks; }
    /** The module lists as the live parameters say (re-read by every change of a watched parameter). */
    void refresh()
    {
        model.values = fx_ops::values (ctx);
        model.racks = fxRacks (model.values.plain);
        bool selectionLost = false;
        if (hasSelection)
        {
            int r = 0, i = 0;
            if (! model.racks.find (selection, r, i))
            {
                hasSelection = false; // the selected slot was removed
                selectionLost = true;
            }
        }
        updateButtons();
        if (selectionLost && onSelectionChanged)
            onSelectionChanged();
        if (onModelChanged)
            onModelChanged();
        const int wanted = heightForWidth (getWidth());
        if (wanted != lastHeight)
        {
            lastHeight = wanted;
            if (onHeightChanged)
                onHeightChanged();
        }
        repaint();
    }

    bool hasSelectedItem() const noexcept { return hasSelection; }
    FxRackItem selectedItem() const noexcept { return selection; }
    /** The slot of the selected pool module, -1 for none or a module of instance 0. */
    int selectedSlot() const noexcept { return hasSelection && selection.pool ? selection.index : -1; }
    void select (FxRackItem item)
    {
        int r = 0, i = 0;
        if (! model.racks.find (item, r, i))
            return;
        const bool changed = ! hasSelection || selection != item;
        selection = item;
        hasSelection = true;
        updateButtons();
        repaint();
        if (changed && onSelectionChanged)
            onSelectionChanged();
    }
    void clearSelection()
    {
        if (hasSelection)
        {
            hasSelection = false;
            updateButtons();
            repaint();
            if (onSelectionChanged)
                onSelectionChanged();
        }
    }

    std::function<void()> onSelectionChanged;
    std::function<void()> onModelChanged;
    std::function<void()> onHeightChanged;

    bool isOn (FxRackItem item) const { return fxItemOn (model.values.plain, item); }
    int typeOf (FxRackItem item) const { return fxItemType (model.values.plain, item); }

    // ---- edits (what the gestures and the buttons call) -----------------------------------------------------------------------------------
    bool togglePower (FxRackItem item)
    {
        const bool done = fx_ops::toggleOn (ctx, item);
        refresh();
        return done;
    }
    void bypassAll()
    {
        fx_ops::bypassAll (ctx, savedOnMask);
        refresh();
    }
    /** The type picker's choice: a module of `type` at the end of `rack`; the new module is selected. Returns the slot or -1 (pool full). */
    int addModule (int type, int rack)
    {
        const int slot = fx_ops::addModule (ctx, type, rack);
        refresh();
        if (slot >= 0)
            select ({ true, slot });
        return slot;
    }
    int duplicate (FxRackItem item)
    {
        const int slot = fx_ops::duplicate (ctx, item);
        refresh();
        if (slot >= 0)
            select ({ true, slot });
        return slot;
    }
    bool removeSlot (int slot)
    {
        const bool done = fx_ops::removeSlot (ctx, slot);
        refresh();
        return done;
    }
    // ---- the bus operations (GROUPA1, WP-10; Serum 2 manual p.158): the clipboard holds the modules of the last cut / copied bus ----------------------------
    int copyBus (int rack)
    {
        return fx_ops::copyBus (ctx, rack, busClip);
    }
    int cutBus (int rack)
    {
        const int n = fx_ops::cutBus (ctx, rack, busClip);
        refresh();
        return n;
    }
    int pasteBus (int rack)
    {
        const int n = fx_ops::pasteBus (ctx, busClip, rack);
        refresh();
        return n;
    }
    int clearBus (int rack)
    {
        const int n = fx_ops::clearBus (ctx, rack);
        refresh();
        return n;
    }
    const FxRackClip& busClipboard() const noexcept { return busClip; }
    juce::TextButton& busMenuButton() noexcept { return busButton; }
    /** The bus menu: for each bus cut / copy / paste / clear; the buttons of the strip open it for the bus of the selected module (Main without one). */
    void showBusMenu (int rack)
    {
        juce::PopupMenu menu;
        for (int r = 0; r < kNumPoolRacks; ++r)
        {
            menu.addSectionHeader (kFxRackNames[r]);
            int modules = 0;
            for (const FxRackItem& item : model.racks.rack[static_cast<size_t> (r)])
                modules += (item.pool || fxItemOn (model.values.plain, item)) && fxItemType (model.values.plain, item) != kFxTypeNone ? 1 : 0;
            menu.addItem (100 + r * 4 + 0, juce::String ("Cut ") + kFxRackNames[r] + " FX Bus", modules > 0);
            menu.addItem (100 + r * 4 + 1, juce::String ("Copy ") + kFxRackNames[r] + " FX Bus", modules > 0);
            menu.addItem (100 + r * 4 + 2, juce::String ("Paste into ") + kFxRackNames[r] + " FX Bus", ! busClip.empty() && fxFirstFreeSlot (model.values.plain) >= 0);
            menu.addItem (100 + r * 4 + 3, juce::String ("Clear ") + kFxRackNames[r] + " FX Bus", modules > 0);
        }
        (void) rack;
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (busButton), [this] (int chosen)
        {
            if (chosen < 100)
                return;
            const int r = (chosen - 100) / 4;
            switch ((chosen - 100) % 4)
            {
                case 0: cutBus (r); break;
                case 1: copyBus (r); break;
                case 2: pasteBus (r); break;
                default: clearBus (r); break;
            }
        });
    }

    bool moveItem (FxRackItem item, int rack, int index)
    {
        const bool done = fx_ops::move (ctx, item, rack, index);
        refresh();
        return done;
    }
    /** One place left (-1) or right (+1) in the rack of the selected module; at the end of the strip it goes on to the next rack. */
    bool moveSelected (int delta)
    {
        if (! hasSelection || delta == 0)
            return false;
        int rack = 0, index = 0;
        if (! model.racks.find (selection, rack, index) || rack >= kNumPoolRacks)
            return false;
        int target = index + delta;
        const int size = static_cast<int> (model.racks.rack[static_cast<size_t> (rack)].size());
        if (target < 0 && rack > 0)
            return moveItem (selection, rack - 1, static_cast<int> (model.racks.rack[static_cast<size_t> (rack - 1)].size()));
        if (target >= size && rack < kNumPoolRacks - 1)
            return moveItem (selection, rack + 1, 0);
        target = juce::jlimit (0, size - 1, target);
        return target != index && moveItem (selection, rack, target);
    }

    void showTypePicker (int rack)
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("Add a module to " + juce::String (kFxRackNames[juce::jlimit (0, 2, rack)]));
        const bool full = fxFirstFreeSlot (model.values.plain) < 0;
        for (int t = 1; t < kNumFxInstanceTypes; ++t)
            menu.addItem (t, fxTypeName (t), ! full);
        if (full)
            menu.addItem (1000, "All 16 slots are in use", false);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, rack] (int chosen)
        {
            if (chosen >= 1 && chosen < kNumFxInstanceTypes)
                addModule (chosen, rack);
        });
    }

    void showCardMenu (FxRackItem item)
    {
        juce::PopupMenu menu;
        menu.addItem (1, isOn (item) ? "Bypass" : "Enable");
        menu.addItem (2, "Duplicate", typeOf (item) != kFxTypeNone && fxFirstFreeSlot (model.values.plain) >= 0);
        if (item.pool)
        {
            menu.addItem (3, "Remove");
            menu.addItem (4, "Copy / Paste / Swap / Init / Lock ...");
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, item] (int chosen)
        {
            switch (chosen)
            {
                case 1: togglePower (item); break;
                case 2: duplicate (item); break;
                case 3: removeSlot (item.index); break;
                case 4: showModuleSheet (ctx, poolPrefix (item.index), *this); break;
                default: break;
            }
        });
    }

    static std::string poolPrefix (int slot)
    {
        char buf[8];
        std::snprintf (buf, sizeof buf, "fxi%02d", slot + 1);
        return buf;
    }

    // ---- geometry (the tests and the gestures use it) ----------------------------------------------------------------------------------
    /** Height of the view at `width`: the strips wrap their cards, so it grows with the number of modules. */
    int heightForWidth (int width) const
    {
        return computeLayout (juce::jmax (1, width)).height;
    }
    juce::Rectangle<float> cardBounds (FxRackItem item) const
    {
        for (const auto& c : layout.cards)
            if (c.item == item)
                return c.bounds;
        return {};
    }
    juce::Rectangle<float> plusBounds (int rack) const
    {
        for (const auto& s : layout.strips)
            if (s.rack == rack)
                return s.plus;
        return {};
    }
    /** Where a drop at `point` would put the dragged item: the rack (0 .. 2) and the index in its list (without the dragged item). */
    bool dropTargetAt (juce::Point<float> point, FxRackItem dragged, int& rack, int& index) const
    {
        const Layout::Strip* best = nullptr;
        float bestDistance = 1.0e9f;
        for (const auto& s : layout.strips)
        {
            if (s.rack >= kNumPoolRacks)
                continue;
            const float d = point.y < s.area.getY() ? s.area.getY() - point.y : (point.y > s.area.getBottom() ? point.y - s.area.getBottom() : 0.0f);
            if (d < bestDistance)
            {
                bestDistance = d;
                best = &s;
            }
        }
        if (best == nullptr)
            return false;
        rack = best->rack;
        index = 0;
        for (const auto& c : layout.cards)
        {
            if (c.rack != best->rack || c.item == dragged)
                continue;
            const bool before = point.y < c.bounds.getY() || (point.y < c.bounds.getBottom() && point.x < c.bounds.getCentreX());
            if (before)
                break;
            ++index;
        }
        return true;
    }
    FxRackItem itemAt (juce::Point<float> point, bool& found) const
    {
        for (const auto& c : layout.cards)
            if (c.bounds.contains (point))
            {
                found = true;
                return c.item;
            }
        found = false;
        return {};
    }

    juce::TextButton& powerToggleButton() noexcept { return powerButton; }
    juce::TextButton& bypassAllToggleButton() noexcept { return bypassAllButton; }
    juce::TextButton& duplicateToggleButton() noexcept { return duplicateButton; }
    juce::TextButton& removeSlotButton() noexcept { return removeButton; }
    juce::TextButton& addModuleButton() noexcept { return addButton; }
    /** FX rack presets (S10-02): the whole chain (modules, order, racks, the pool slots) saved to and loaded from the user library. */
    juce::TextButton& rackPresetsButton() noexcept { return presetsButton; }
    LibraryMenu& rackPresets() noexcept { return *rackLibrary; }
    void initRackPresets()
    {
        LibraryMenuSpec spec;
        spec.category = kLibFxRacks;
        spec.noun = "FX rack";
        spec.suggestedName = "My rack";
        spec.makeItem = [this] (const juce::String& name) { return serializeFxRackPreset (name.toStdString(), readLiveValues (ctx)); };
        spec.applyItem = [this] (const juce::String&, const std::string& json)
        {
            ParamValues values = readLiveValues (ctx);
            if (applyFxRackPreset (json, values) == 0)
                return false;
            applyValues (ctx, values);
            refresh();
            return true;
        };
        rackLibrary = std::make_shared<LibraryMenu> (ctx, std::move (spec));
        presetsButton.onClick = [this] { rackLibrary->show (presetsButton); };
    }
    juce::TextButton& moveLeft() noexcept { return moveLeftButton; }
    juce::TextButton& moveRight() noexcept { return moveRightButton; }
    LongPressDetector& longPress() noexcept { return press; }
    bool reorderIsArmed() const noexcept { return reorderArmed; }
    bool dropLineShown() const noexcept { return reorderArmed && isDragging && dropRack >= 0; }
    int dropRackShown() const noexcept { return dropRack; }
    int dropIndexShown() const noexcept { return dropIndex; }

    // ---- component ------------------------------------------------------------------------------------------------------------------
    void resized() override
    {
        layout = computeLayout (getWidth());
        const auto& t = ctx.theme;
        const int gap = t.gap(), h = t.touch();
        auto area = getLocalBounds().reduced (t.ipx (6.0f), 0);
        auto row2 = area.removeFromBottom (h);
        area.removeFromBottom (gap);
        auto row1 = area.removeFromBottom (h);
        area.removeFromBottom (gap);
        const auto split = [gap] (juce::Rectangle<int> row, std::initializer_list<juce::Component*> parts)
        {
            const int n = static_cast<int> (parts.size());
            const int w = (row.getWidth() - (n - 1) * gap) / n;
            for (auto* p : parts)
            {
                p->setBounds (row.removeFromLeft (w));
                row.removeFromLeft (gap);
            }
        };
        split (row1, { &moveLeftButton, &moveRightButton, &powerButton, &bypassAllButton });
        split (row2, { &duplicateButton, &removeButton, &addButton, &presetsButton, &busButton });
    }

    void paint (juce::Graphics& g) override
    {
        paintBaseBackground (g);
        const auto& t = ctx.theme;

        for (const auto& s : layout.strips)
        {
            g.setColour (t.colours.textDim);
            g.setFont (t.font (t.px (10.0f), true));
            g.drawText (s.title, s.header.toNearestInt(), juce::Justification::centredLeft, false);
            g.setColour (t.colours.outline.withAlpha (0.5f));
            g.drawRoundedRectangle (s.area.reduced (0.5f), t.px (4.0f), 1.0f);
        }

        for (const auto& c : layout.cards)
        {
            const bool selected = hasSelection && selection == c.item;
            const bool on = fxItemOn (model.values.plain, c.item);
            const bool grabbed = reorderArmed && dragItemValid && dragItem == c.item;
            const auto r = c.bounds;
            g.setColour (selected ? t.colours.accentDim.withAlpha (0.35f) : t.colours.panel);
            g.fillRoundedRectangle (r, t.px (4.0f));
            g.setColour (grabbed ? t.colours.mod : (selected ? t.colours.accent : (c.item.pool ? t.colours.mod.withAlpha (0.7f) : t.colours.outline)));
            g.drawRoundedRectangle (r, t.px (4.0f), grabbed ? t.px (3.0f) : (selected ? t.px (2.0f) : 1.0f));

            const float dotRadius = t.px (3.5f);
            const juce::Point<float> dot (r.getX() + t.px (6.0f) + dotRadius, r.getY() + t.px (6.0f) + dotRadius);
            g.setColour (on ? t.colours.accent : t.colours.warn);
            g.fillEllipse (dot.x - dotRadius, dot.y - dotRadius, dotRadius * 2.0f, dotRadius * 2.0f);

            g.setColour (on ? t.colours.text : t.colours.textDim);
            g.setFont (t.font (t.px (10.0f), true));
            const int module = c.item.pool ? fxTypeDescriptor (fxItemType (model.values.plain, c.item)).module : c.item.index;
            g.drawText (module >= 0 ? kFxModuleShortNames[module] : "?", r.withTrimmedLeft (t.px (16.0f)).withTrimmedBottom (r.getHeight() * 0.4f),
                        juce::Justification::centredLeft, false);
            g.setColour (t.colours.textDim);
            g.setFont (t.font (t.px (9.0f)));
            const juce::String tag = c.item.pool ? "slot " + juce::String (c.item.index + 1)
                                                 : juce::String (model.racks.position[static_cast<size_t> (c.item.index)] + 1) + ".";
            g.drawText (tag, r.withTrimmedLeft (t.px (16.0f)).withTrimmedTop (r.getHeight() * 0.55f), juce::Justification::centredLeft, false);
        }

        for (const auto& s : layout.strips)
        {
            if (s.rack >= kNumPoolRacks)
                continue;
            g.setColour (t.colours.accent);
            g.setFont (t.font (t.px (18.0f), true));
            g.drawText ("+", s.plus, juce::Justification::centred, false);
            g.setColour (t.colours.outline);
            g.drawRoundedRectangle (s.plus, t.px (4.0f), 1.0f);
        }

        if (dropLineShown())
        {
            int index = 0;
            juce::Rectangle<float> beside;
            bool before = true;
            for (const auto& c : layout.cards)
            {
                if (c.rack != dropRack || (dragItemValid && c.item == dragItem))
                    continue;
                if (index == dropIndex)
                {
                    beside = c.bounds;
                    break;
                }
                beside = c.bounds;
                before = false;
                ++index;
            }
            if (beside.isEmpty())
                for (const auto& s : layout.strips)
                    if (s.rack == dropRack)
                        beside = s.plus;
            const float x = before ? beside.getX() - t.px (2.0f) : beside.getRight() + t.px (2.0f);
            g.setColour (t.colours.accent);
            g.drawLine (x, beside.getY(), x, beside.getBottom(), t.px (3.0f));
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        reorderArmed = false;
        isDragging = false;
        dragItemValid = false;
        dropRack = dropIndex = -1;
        bool found = false;
        const auto item = itemAt (e.position, found);
        if (found)
        {
            select (item);
            dragItem = item;
            dragItemValid = true;
            press.press (e.position);
            // a tap on the power dot of the card (Opt/Alt-tap: every module)
            if (e.position.x < cardBounds (item).getX() + ctx.theme.px (18.0f))
            {
                press.cancel();
                dragItemValid = false;
                if (e.mods.isAltDown())
                    bypassAll();
                else
                    togglePower (item);
            }
            return;
        }
        for (const auto& s : layout.strips)
            if (s.rack < kNumPoolRacks && s.plus.contains (e.position))
                plusRack = s.rack;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragItemValid)
            return;
        if (! reorderArmed)
        {
            press.moved (e.position); // a drag before the hold is no reorder
            return;
        }
        isDragging = true;
        int rack = 0, index = 0;
        if (dropTargetAt (e.position, dragItem, rack, index) && (rack != dropRack || index != dropIndex))
        {
            dropRack = rack;
            dropIndex = index;
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        press.cancel();
        const bool wasArmed = reorderArmed, moved = isDragging;
        const bool valid = dragItemValid;
        const FxRackItem item = dragItem;
        const int rack = dropRack, index = dropIndex;
        reorderArmed = false;
        isDragging = false;
        dragItemValid = false;
        dropRack = dropIndex = -1;
        if (valid && wasArmed && moved && rack >= 0)
            moveItem (item, rack, index);
        else if (valid && wasArmed)
            showCardMenu (item); // held, then let go without moving: the card menu
        else if (plusRack >= 0 && plusBounds (plusRack).contains (e.position))
            showTypePicker (plusRack);
        plusRack = -1;
        repaint();
    }

    /** Double tap anywhere on a card: its power. */
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        bool found = false;
        const auto item = itemAt (e.position, found);
        press.cancel();
        reorderArmed = false;
        dragItemValid = false;
        if (found)
        {
            if (e.mods.isAltDown())
                bypassAll();
            else
                togglePower (item);
        }
    }

private:
    struct Layout
    {
        struct Card
        {
            FxRackItem item;
            int rack;
            juce::Rectangle<float> bounds;
        };
        struct Strip
        {
            int rack;
            juce::String title;
            juce::Rectangle<float> header, area, plus;
        };
        std::vector<Card> cards;
        std::vector<Strip> strips;
        int height = 0;
    };

    struct Model
    {
        ParamValues values = ParamValues::defaults();
        FxRacks racks;
        int rackOf (FxRackItem item) const
        {
            int r = 0, i = 0;
            return racks.find (item, r, i) ? r : 0;
        }
    };

    Layout computeLayout (int width) const
    {
        const auto& t = ctx.theme;
        Layout out;
        const float pad = t.px (6.0f);
        const float gap = t.px (4.0f);
        const float cardH = static_cast<float> (t.touch());
        const float avail = static_cast<float> (width) - 2.0f * pad;
        const int cols = juce::jmax (2, static_cast<int> ((avail + gap) / (t.px (72.0f) + gap)));
        const float cardW = (avail - 2.0f * t.px (4.0f) - gap * static_cast<float> (cols - 1)) / static_cast<float> (cols);
        float y = static_cast<float> (t.ipx (20.0f));
        for (int rack = 0; rack < kNumFxRacks; ++rack)
        {
            const auto& list = model.racks.rack[static_cast<size_t> (rack)];
            if (rack >= kNumPoolRacks && list.empty())
                continue;
            const int count = static_cast<int> (list.size()) + (rack < kNumPoolRacks ? 1 : 0);
            const int rows = juce::jmax (1, (count + cols - 1) / cols);
            Layout::Strip strip;
            strip.rack = rack;
            strip.title = juce::String (kFxRackNames[rack]).toUpperCase() + "  (" + juce::String (static_cast<int> (list.size())) + ")";
            const float headerH = t.px (14.0f);
            strip.header = { pad + t.px (4.0f), y, avail, headerH };
            const float bodyH = static_cast<float> (rows) * (cardH + gap) + t.px (4.0f);
            strip.area = { pad, y + headerH, avail, bodyH };
            for (int i = 0; i < count; ++i)
            {
                const juce::Rectangle<float> r (pad + t.px (4.0f) + static_cast<float> (i % cols) * (cardW + gap),
                                                y + headerH + t.px (2.0f) + static_cast<float> (i / cols) * (cardH + gap), cardW, cardH);
                if (i < static_cast<int> (list.size()))
                    out.cards.push_back ({ list[static_cast<size_t> (i)], rack, r });
                else
                    strip.plus = r;
            }
            out.strips.push_back (strip);
            y += headerH + bodyH + t.px (4.0f);
        }
        out.height = static_cast<int> (std::ceil (y)) + 2 * (t.touch() + t.gap()) + t.ipx (4.0f);
        return out;
    }

    void updateButtons()
    {
        const bool any = hasSelection;
        const bool pool = any && selection.pool;
        powerButton.setEnabled (any);
        powerButton.setButtonText (any && isOn (selection) ? "ON" : "OFF");
        powerButton.setToggleState (any && isOn (selection), juce::dontSendNotification);
        moveLeftButton.setEnabled (any);
        moveRightButton.setEnabled (any);
        duplicateButton.setEnabled (any && typeOf (selection) != kFxTypeNone && fxFirstFreeSlot (model.values.plain) >= 0);
        removeButton.setEnabled (pool);
        addButton.setEnabled (fxFirstFreeSlot (model.values.plain) >= 0);
        layout = computeLayout (getWidth());
    }

    std::vector<std::unique_ptr<ParamHandle>> watchers;
    Model model;
    Layout layout;
    int lastHeight = 0;

    juce::TextButton moveLeftButton, moveRightButton, powerButton, bypassAllButton, duplicateButton, removeButton, addButton, presetsButton, busButton;
    FxRackClip busClip;
    std::shared_ptr<LibraryMenu> rackLibrary;
    LongPressDetector press;
    bool reorderArmed = false, isDragging = false;
    FxRackItem selection, dragItem;
    bool hasSelection = false, dragItemValid = false;
    int dropRack = -1, dropIndex = -1, plusRack = -1;
    uint32_t savedOnMask = 0;
};
} // namespace synthyboy::ui
