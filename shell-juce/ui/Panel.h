// SynthyBoy - UI: Panel base class and the self-registering panel registry.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A panel is ONE .cpp file in shell-juce/ui/panels/ that ends with ONE registration line:
//
//     SYNTHYBOY_REGISTER_PANEL (MasterVoicingPanel, Page::glb, 10)
//
// CMake globs panels/*.cpp, so nothing else has to be touched (see README.md).
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "GestureKit.h"
#include "ParamControls.h"

namespace synthyboy::ui
{
class Panel : public juce::Component, private juce::ComponentListener
{
public:
    explicit Panel (UiContext& context);
    ~Panel() override;

    /** Panel title (shown in the panel header and in the jump list). */
    virtual juce::String title() const = 0;
    /** Every key the panel shows (primary controls first, then the "more" row). The test suite checks that this list is what build()
        creates and that every key exists in the parameter table. */
    virtual std::vector<std::string> keys() const = 0;
    /** Creates the controls (addControl / addMoreControl) and any custom visual. Called once, after construction. */
    virtual void build() = 0;

    /** Height the panel wants at `width` (default: the flow layout of the controls). A panel with a big visual overrides this. */
    virtual int preferredHeight (int width) const;

    /** The controls, in creation order (primary + more). */
    const std::vector<std::unique_ptr<ParamControl>>& controls() const noexcept { return owned; }
    bool isMoreExpanded() const noexcept { return moreExpanded; }
    void setMoreExpanded (bool shouldExpand);
    int numMoreControls() const noexcept { return static_cast<int> (moreCount); }

    /** True while the panel is folded away by a tab chip row of its page (LFO tabs): the PanelHost does not lay it out. */
    virtual bool hiddenInLayout() const { return false; }

    /** "osc1", "filter2", "env3", "lfo4", "rlfo2" for a panel that is a module (empty = not a module). A module panel has the module sheet:
        long-press on the header or the "..." button in it; a header drag onto the header of another module of the same kind swaps
        the two (Alt held = copy, Alt + Shift = copy with modulation). */
    virtual std::string modulePrefix() const { return {}; }
    /** The "..." button of the header (null for a panel that is not a module, or before it has a parent). */
    juce::Component* moduleButton() noexcept { return moduleMenuButton.get(); }
    juce::Rectangle<int> headerBounds() const;
    LongPressDetector& headerLongPress() noexcept { return headerPress; }
    /** End of a header drag (the mouse handler calls it; the tests call it directly): swap / copy onto the module panel at `parentPos`. */
    bool dropModuleOn (juce::Point<int> parentPos, bool copy, bool withMods);
    /** The long-press of the header (the timer calls it; the tests call it). */
    void headerLongPressed();

    /** Set by the PanelHost: called when the layout changed (preferredHeight() changed: More expanded / collapsed; a tab was chosen). */
    std::function<void()> onHeightChanged;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void parentHierarchyChanged() override;

protected:
    /** Primary control for `key` (knob / toggle / choice by ParamInfo). Principle 1 of the brief: at most 4 of these next to a visual. */
    ParamControl& addControl (const std::string& key, const juce::String& label = {});
    /** Control behind the panel's "more" toggle. */
    ParamControl& addMoreControl (const std::string& key, const juce::String& label = {});
    /** A control of a custom class behind "More" (the OCT / SEM control, a grouped list ...). */
    ParamControl& addMoreControl (std::unique_ptr<ParamControl> control);

    /** The flow layout of the controls inside `area`; returns the height used. `measureOnly` lays nothing out. Panels with their own
        visual call it for the area below the visual. */
    int layoutControls (juce::Rectangle<int> area, bool measureOnly);
    juce::Rectangle<int> contentArea() const;
    int headerHeight() const;

    UiContext& ctx;

private:
    void componentMovedOrResized (juce::Component&, bool, bool) override;
    void ensureModuleButton();
    ParamControl& adoptMore (ParamControl& control);

    std::unique_ptr<ModuleMenuButton> moduleMenuButton;
    LongPressDetector headerPress;
    bool headerDown = false, headerDragging = false;
    std::vector<std::unique_ptr<ParamControl>> owned;
    size_t moreCount = 0;
    bool moreExpanded = false;
    juce::TextButton moreButton { "More" };
    bool moreButtonAdded = false;
};

using PanelFactory = std::function<std::unique_ptr<Panel> (UiContext&)>;

struct PanelEntry
{
    std::string id;     // class name, for diagnostics
    Page page;          // tab it belongs to
    int order;          // position inside the page (and in the wide grid), ascending
    PanelFactory create;
};

class PanelRegistry
{
public:
    static PanelRegistry& instance();
    void add (PanelEntry entry);
    /** All panels sorted by (page, order, id). */
    const std::vector<PanelEntry>& entries() const;

private:
    mutable std::vector<PanelEntry> list;
    mutable bool sorted = true;
};

struct PanelRegistrar
{
    PanelRegistrar (const char* id, Page page, int order, PanelFactory factory)
    {
        PanelRegistry::instance().add ({ id, page, order, std::move (factory) });
    }
};
} // namespace synthyboy::ui

/** The one registration line of a panel file: class, page tab, order inside the page. */
#define SYNTHYBOY_REGISTER_PANEL(PanelClass, PageId, Order)                                                              \
    static const ::synthyboy::ui::PanelRegistrar PanelClass##_registrar (#PanelClass, ::synthyboy::ui::PageId, Order,   \
        [] (::synthyboy::ui::UiContext& context) -> std::unique_ptr<::synthyboy::ui::Panel> { return std::make_unique<PanelClass> (context); });
