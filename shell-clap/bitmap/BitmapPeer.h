// SynthyBoy - bitmap GUI: a juce::ComponentPeer that is a memory buffer, not a window.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The editor lives in a Component that is "on the desktop" with this peer. That gives it everything a window gives a JUCE component without a windowing system: isShowing (),
// mouse / touch / key dispatch through ComponentPeer::handleMouseEvent / handleKeyPress, keyboard focus, and repaint (area) which is exactly the dirty rectangle list we need. The pixels are
// drawn by BitmapSession into a software image; nothing here reaches a native window, a display server or the host's event loop.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace synthyboy::bitmap
{
class BitmapPeer final : public juce::ComponentPeer
{
public:
    /** What the peer tells the session. All calls are on the message thread. */
    struct Client
    {
        virtual ~Client() = default;
        virtual void peerRepaint (const juce::Rectangle<int>& area) = 0;
        virtual void peerTextInput (bool show, juce::TextInputTarget* target) = 0;
    };

    BitmapPeer (juce::Component& peerComponent, int flags, Client& owner);
    ~BitmapPeer() override;

    void* getNativeHandle() const override { return nullptr; }
    void setVisible (bool shouldBeVisible) override { visible = shouldBeVisible; }
    void setTitle (const juce::String&) override {}
    void setBounds (const juce::Rectangle<int>& newBounds, bool) override;
    juce::Rectangle<int> getBounds() const override { return bounds; }
    juce::Point<float> localToGlobal (juce::Point<float> p) override { return p + bounds.getPosition().toFloat(); }
    using juce::ComponentPeer::localToGlobal;
    juce::Point<float> globalToLocal (juce::Point<float> p) override { return p - bounds.getPosition().toFloat(); }
    using juce::ComponentPeer::globalToLocal;
    void setMinimised (bool) override {}
    bool isMinimised() const override { return false; }
    bool isShowing() const override { return visible; }
    void setFullScreen (bool) override {}
    bool isFullScreen() const override { return false; }
    void setIcon (const juce::Image&) override {}
    bool contains (juce::Point<int> localPos, bool) const override { return bounds.withZeroOrigin().contains (localPos); }
    OptionalBorderSize getFrameSizeIfPresent() const override { return {}; }
    juce::BorderSize<int> getFrameSize() const override { return {}; }
    bool setAlwaysOnTop (bool) override { return true; }
    void toFront (bool takeKeyboardFocus) override;
    void toBehind (juce::ComponentPeer*) override {}
    bool isFocused() const override { return focused; }
    void grabFocus() override;
    void repaint (const juce::Rectangle<int>& area) override;
    void performAnyPendingRepaintsNow() override {}
    void setAlpha (float) override {}
    juce::StringArray getAvailableRenderingEngines() override { return { "Software Renderer" }; }

    /** The host says the GUI has / has lost the keyboard focus. */
    void setHostFocus (bool hasFocus);

private:
    void textInputRequired (juce::Point<int>, juce::TextInputTarget& target) override;
    void dismissPendingTextInput() override;

    Client& client;
    juce::Rectangle<int> bounds;
    bool visible = false, focused = false;
};
} // namespace synthyboy::bitmap
