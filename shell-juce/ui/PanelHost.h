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

    /** The toolkit's own widgets (buttons, sliders, combo boxes, text fields) use the press and the drag, and a button clicks on the release if the finger is still over it:
        a drag that starts on one must not scroll the area (the content would follow the finger, the release would click). They cannot mark themselves (they are JUCE
        classes, made anywhere in the panels), so the press is watched for the whole content, nested areas included, and marks the widget it landed on
        (Component::setViewportIgnoreDragFlag, read when the drag starts). The components of this project that use the drag mark themselves in their constructors. */
    class StockWidgetGuard final : private juce::MouseListener
    {
    public:
        explicit StockWidgetGuard (juce::Component& rootToWatch) : root (rootToWatch) { root.addMouseListener (this, true); }
        ~StockWidgetGuard() override { root.removeMouseListener (this); }

        static bool isStockWidget (const juce::Component& c) noexcept
        {
            return dynamic_cast<const juce::Button*> (&c) != nullptr || dynamic_cast<const juce::Slider*> (&c) != nullptr
                   || dynamic_cast<const juce::ComboBox*> (&c) != nullptr || dynamic_cast<const juce::TextEditor*> (&c) != nullptr
                   || dynamic_cast<const juce::ScrollBar*> (&c) != nullptr;
        }

    private:
        void mouseDown (const juce::MouseEvent& e) override
        {
            for (auto* c = e.originalComponent; c != nullptr && c != &root; c = c->getParentComponent())
                if (isStockWidget (*c))
                    c->setViewportIgnoreDragFlag (true);
        }

        juce::Component& root;
    };

    void layoutPanels();

    UiContext& ctx;
    std::vector<std::unique_ptr<Panel>> panels;
    std::vector<Page> panelPages;
    juce::Viewport viewport;
    Content content;
    StockWidgetGuard stockWidgetGuard { content };
    Page currentPage = Page::glb;
    LayoutMode layoutMode = LayoutMode::wide;
};
} // namespace synthyboy::ui
