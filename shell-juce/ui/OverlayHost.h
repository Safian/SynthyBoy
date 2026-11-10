// SynthyBoy - UI: where overlays live (loupe, value bubble, toast, sheets, call-outs, pop-up menus, text prompt).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The editor is a plain Component that can sit anywhere in a host's component tree (the plug-in window, the Kick PluginPanel). Every overlay is therefore
// attached to the editor itself (OverlayHost::overlayRoot), never to getTopLevelComponent (), which in an embedded editor is the whole host window.
// Pop-up menus and call-outs are desktop level windows in JUCE unless a parent is given; UiHost::overlaysInsideEditor () decides (default true: a host
// without native windows, such as the software framebuffer of the Akai MPC, has none to give them).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace synthyboy::ui
{
/** Implemented by the editor: the Component the overlays are added to, and whether pop-up menus / call-outs stay inside it. */
class OverlayHost
{
public:
    virtual ~OverlayHost() = default;
    virtual juce::Component& overlayRoot() noexcept = 0;
    virtual bool confineOverlays() const noexcept = 0;
};

/** The editor `c` belongs to (null for a component outside an editor, such as a control built by a test). */
inline OverlayHost* overlayHostOf (const juce::Component& c) { return c.findParentComponentOfClass<OverlayHost>(); }

/** The Component overlays of `c` are added to: the editor, else the top level component (a control that is not in an editor yet). */
inline juce::Component* overlayRootOf (juce::Component& c)
{
    if (auto* host = overlayHostOf (c))
        return &host->overlayRoot();
    return c.getTopLevelComponent();
}

/** `options` of a pop-up menu that belongs to `owner`: inside the editor when the host wants that, else a desktop level window as JUCE makes it. */
inline juce::PopupMenu::Options withOverlayParent (juce::PopupMenu::Options options, const juce::Component& owner)
{
    if (auto* host = overlayHostOf (owner); host != nullptr && host->confineOverlays())
        options = options.withParentComponent (&host->overlayRoot());
    return options;
}

/** Options of a pop-up menu that opens at `target`. */
inline juce::PopupMenu::Options popupOptions (juce::Component& target)
{
    return withOverlayParent (juce::PopupMenu::Options().withTargetComponent (&target), target);
}
inline juce::PopupMenu::Options popupOptions (juce::Component* target) { return popupOptions (*target); }

/** Opens `content` in a call-out box pointing at `anchor`. */
inline juce::CallOutBox& launchCallOut (std::unique_ptr<juce::Component> content, juce::Component& anchor)
{
    if (auto* host = overlayHostOf (anchor); host != nullptr && host->confineOverlays())
    {
        auto& root = host->overlayRoot();
        return juce::CallOutBox::launchAsynchronously (std::move (content), root.getLocalArea (&anchor, anchor.getLocalBounds()), &root);
    }
    return juce::CallOutBox::launchAsynchronously (std::move (content), anchor.getScreenBounds(), nullptr);
}
} // namespace synthyboy::ui
