// SynthyBoy - UI: PanelHost, the page tabs and the width-switched layout of the panels.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "Panel.h"

namespace synthyboy::ui
{
/** The row of page tabs (OSC MIX FLT ENV LFO MOD FX GLB). Buttons are at least 44 wide; narrower hosts scroll sideways. */
class PageTabs final : public juce::Component
{
public:
    explicit PageTabs (UiContext& context);

    Page selected() const noexcept { return current; }
    void select (Page page, bool notify = true);
    void setHasPanels (Page page, bool has);
    /** A short read-out on a tab ("GLB 3": the sounding voices on the GLOBAL tab); empty text takes it away. */
    void setBadge (Page page, const juce::String& text);
    std::function<void (Page)> onSelect;

    void resized() override;
    const juce::TextButton& button (Page page) const { return *buttons[static_cast<size_t> (page)]; }

private:
    UiContext& ctx;
    juce::Viewport viewport;
    juce::Component strip;
    std::vector<std::unique_ptr<juce::TextButton>> buttons;
    Page current = Page::osc;
};

/** Holds every registered panel. phone (< 600): one column with the panels of the selected page. tablet (600..999): two columns, same.
    wide (>= 1000): ALL panels as tiles in three columns (Vital-like grid); the tabs scroll to the first tile of their page. */
class PanelHost final : public juce::Component
{
public:
    explicit PanelHost (UiContext& context);
    ~PanelHost() override;

    LayoutMode mode() const noexcept { return layoutMode; }
    void setPage (Page page);
    Page page() const noexcept { return currentPage; }
    /** Panels shown at the moment, in layout order. */
    std::vector<Panel*> visiblePanels() const;
    const std::vector<std::unique_ptr<Panel>>& allPanels() const noexcept { return panels; }
    bool pageHasPanels (Page page) const;
    /** Total scrollable content height at the current width (for the screenshot test). */
    int contentHeight() const noexcept { return content.getHeight(); }

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class Content final : public juce::Component
    {
    public:
        void paint (juce::Graphics&) override {}
    };

    void layoutPanels();

    UiContext& ctx;
    std::vector<std::unique_ptr<Panel>> panels;
    std::vector<Page> panelPages;
    juce::Viewport viewport;
    Content content;
    Page currentPage = Page::glb;
    LayoutMode layoutMode = LayoutMode::wide;
};
} // namespace synthyboy::ui
