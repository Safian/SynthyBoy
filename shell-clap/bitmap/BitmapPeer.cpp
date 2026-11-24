// SynthyBoy - bitmap GUI: a juce::ComponentPeer that is a memory buffer (see BitmapPeer.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "BitmapPeer.h"

namespace synthyboy::bitmap
{
BitmapPeer::BitmapPeer (juce::Component& peerComponent, int flags, Client& owner)
    : juce::ComponentPeer (peerComponent, flags), client (owner), bounds (peerComponent.getBounds().withZeroOrigin())
{
}

BitmapPeer::~BitmapPeer()
{
    client.peerTextInput (false, nullptr);
}

void BitmapPeer::setBounds (const juce::Rectangle<int>& newBounds, bool)
{
    const bool resizedNow = newBounds.getWidth() != bounds.getWidth() || newBounds.getHeight() != bounds.getHeight();
    bounds = newBounds.withPosition (0, 0); // the GUI is the whole of its buffer: its screen position is always the origin
    handleMovedOrResized();
    if (resizedNow)
        client.peerRepaint (bounds.withZeroOrigin());
}

void BitmapPeer::toFront (bool takeKeyboardFocus)
{
    if (takeKeyboardFocus)
        grabFocus();
}

void BitmapPeer::grabFocus()
{
    if (! focused)
    {
        focused = true;
        handleFocusGain();
    }
}

void BitmapPeer::setHostFocus (bool hasFocus)
{
    if (hasFocus == focused)
        return;
    focused = hasFocus;
    if (hasFocus)
        handleFocusGain();
    else
        handleFocusLoss();
}

void BitmapPeer::repaint (const juce::Rectangle<int>& area)
{
    const auto clipped = area.getIntersection (bounds.withZeroOrigin());
    if (! clipped.isEmpty())
        client.peerRepaint (clipped);
}

void BitmapPeer::textInputRequired (juce::Point<int>, juce::TextInputTarget& target)
{
    client.peerTextInput (true, &target);
}

void BitmapPeer::dismissPendingTextInput()
{
    closeInputMethodContext();
    client.peerTextInput (false, nullptr);
}
} // namespace synthyboy::bitmap
