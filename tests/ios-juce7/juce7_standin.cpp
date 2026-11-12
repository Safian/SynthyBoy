// SynthyBoy - the part of the Kick app that is JUCE 7 (tests/ios-juce7/CMakeLists.txt): the message manager, a component painted by the software renderer, a MidiKeyboardComponent (the
// juce_audio_utils module), all in the namespace juce of JUCE 7.0.12, not renamed.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <CoreFoundation/CoreFoundation.h>

#include <cstdio>

namespace
{
struct Swatch final : juce::Component
{
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff204060));
        g.setColour (juce::Colours::orange);
        g.fillEllipse (getLocalBounds().toFloat().reduced (8.0f));
        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (20.0f));
        g.drawText ("JUCE 7", getLocalBounds(), juce::Justification::centred);
    }
};
} // namespace

/** Runs JUCE 7 for a moment; returns 0 when everything behaved, a number that says what did not otherwise. */
int juce7StandInRound()
{
    if (juce::MessageManager::getInstanceWithoutCreating() == nullptr)
        juce::initialiseJuce_GUI();
    if (JUCE_MAJOR_VERSION != 7)
        return 2;

    Swatch swatch;
    swatch.setBounds (0, 0, 120, 80);
    juce::Image image (juce::Image::ARGB, 120, 80, true, juce::SoftwareImageType());
    {
        juce::Graphics g (image);
        swatch.paintEntireComponent (g, false);
    }
    if (image.getPixelAt (60, 40).getAlpha() != 255 || image.getPixelAt (1, 1) != juce::Colour (0xff204060))
        return 3;

    juce::MidiKeyboardState state;
    juce::MidiKeyboardComponent keyboard (state, juce::MidiKeyboardComponent::horizontalKeyboard);
    keyboard.setBounds (0, 0, 400, 60);
    juce::Image keys (juce::Image::ARGB, 400, 60, true, juce::SoftwareImageType());
    {
        juce::Graphics g (keys);
        keyboard.paintEntireComponent (g, false);
    }
    if (keys.getPixelAt (5, 30).getAlpha() == 0)
        return 4;

    bool timerFired = false;
    struct OneShot final : juce::Timer
    {
        explicit OneShot (bool& flag) : fired (flag) {}
        void timerCallback() override { fired = true; stopTimer(); }
        bool& fired;
    } shot (timerFired);
    shot.startTimer (5);
    const auto until = juce::Time::getMillisecondCounter() + 500;
    while (! timerFired && juce::Time::getMillisecondCounter() < until)
        CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.02, true); // the message manager of JUCE 7 posts into this run loop (a modal loop is not permitted on iOS)
    return timerFired ? 0 : 5;
}

void juce7StandInShutdown()
{
    juce::shutdownJuce_GUI();
}
