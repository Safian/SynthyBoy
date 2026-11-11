// SynthyBoy - bitmap GUI: the JUCE runtime of this module (see JuceRuntime.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "JuceRuntime.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <mutex>

#include "BinaryData.h"

#if JUCE_MAC || JUCE_IOS
 #include <CoreFoundation/CoreFoundation.h>
#elif JUCE_LINUX || JUCE_BSD || JUCE_WINDOWS
namespace juce::detail
{
// juce_events/native/juce_Messaging_linux.cpp, juce_Messaging_windows.cpp (JUCE 8.0.17, compiled into this module): one pass over the toolkit's own queue
bool dispatchNextMessageOnSystemQueue (bool returnIfNoPendingMessages);
} // namespace juce::detail
#endif

#if JUCE_LINUX || JUCE_BSD
// juce::Process::isForegroundProcess () on Linux answers "an X window of this process has the focus" (LinuxComponentPeer::isActiveApplication), set by the X11 peer. A pop-up menu closes
// itself at once when the process is not in the foreground (juce_PopupMenu.cpp, doesAnyJuceCompHaveFocus), so on a Linux host without X11 (the framebuffer of the Akai MPC) no menu would
// ever stay open. The flag is a plain static data member inside this module's own copy of JUCE: the declaration below names that symbol and nothing else of the class.
namespace juce
{
class LinuxComponentPeer
{
public:
    static bool isActiveApplication;
};
} // namespace juce
#endif

namespace synthyboy::bitmap
{
namespace
{
std::mutex& runtimeMutex()
{
    static std::mutex m;
    return m;
}

int s_sessions = 0;
bool s_started = false;

/** The look and feel every component without one of its own gets (menus, text fields, the tooltip): the embedded Roboto, so the glyphs do not depend on what the machine has installed
    (no fontconfig, no system font directory: the MPC has none). */
class EmbeddedFontLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    EmbeddedFontLookAndFeel()
        : regular (juce::Typeface::createSystemTypefaceFor (BinaryData::RobotoRegular_ttf, static_cast<size_t> (BinaryData::RobotoRegular_ttfSize))),
          bold (juce::Typeface::createSystemTypefaceFor (BinaryData::RobotoBold_ttf, static_cast<size_t> (BinaryData::RobotoBold_ttfSize)))
    {
    }

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font& font) override
    {
        const bool wantsBold = font.isBold() || font.getTypefaceStyle().containsIgnoreCase ("bold");
        if (auto face = wantsBold ? bold : regular; face != nullptr)
            return face;
        return juce::LookAndFeel_V4::getTypefaceForFont (font);
    }

private:
    juce::Typeface::Ptr regular, bold;
};

std::unique_ptr<EmbeddedFontLookAndFeel> s_lookAndFeel;

/** A host without a display (the framebuffer of the MPC has no X server) leaves JUCE with an empty list of displays, and parts of JUCE assume there is one (the pop-up menu reads the safe area of
    the display it opens on). The GUI lives in memory, so it gets a virtual display, large enough for every size the GUI can have. */
void ensureADisplay()
{
    auto& displays = const_cast<juce::Displays&> (juce::Desktop::getInstance().getDisplays());
    if (! displays.displays.isEmpty())
        return;
    juce::Displays::Display display;
    display.isMain = true;
    display.scale = 1.0;
    display.dpi = 96.0;
    display.logicalBounds = juce::Rectangle<float> (0.0f, 0.0f, 4096.0f, 4096.0f);
    display.userBounds = display.logicalBounds;
    display.physicalBounds = juce::Rectangle<int> (0, 0, 4096, 4096);
    JUCE_BEGIN_IGNORE_DEPRECATION_WARNINGS
    display.totalArea = display.physicalBounds;
    display.userArea = display.physicalBounds;
    display.topLeftPhysical = { 0, 0 };
    JUCE_END_IGNORE_DEPRECATION_WARNINGS
    displays.displays.add (display);
}
} // namespace

void JuceRuntime::acquire()
{
    const std::lock_guard<std::mutex> lock (runtimeMutex());
    if (! s_started)
    {
        juce::initialiseJuce_GUI();
        s_started = true;
    }
    enter();
    ensureADisplay();
   #if JUCE_LINUX || JUCE_BSD
    juce::LinuxComponentPeer::isActiveApplication = true; // a GUI of this module exists: its pop-up menus must stay open (see above)
   #endif
    if (s_lookAndFeel == nullptr)
    {
        s_lookAndFeel = std::make_unique<EmbeddedFontLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel (s_lookAndFeel.get());
    }
    ++s_sessions;
}

void JuceRuntime::release() noexcept
{
    const std::lock_guard<std::mutex> lock (runtimeMutex());
    if (s_sessions > 0)
        --s_sessions;
}

void JuceRuntime::shutdown() noexcept
{
    const std::lock_guard<std::mutex> lock (runtimeMutex());
    if (! s_started || s_sessions > 0)
        return;
    enter();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    s_lookAndFeel.reset();
    juce::shutdownJuce_GUI();
    s_started = false;
}

int JuceRuntime::activeSessions() noexcept
{
    const std::lock_guard<std::mutex> lock (runtimeMutex());
    return s_sessions;
}

void JuceRuntime::enter() noexcept
{
    if (auto* manager = juce::MessageManager::getInstanceWithoutCreating())
        if (! manager->isThisTheMessageThread())
            manager->setCurrentThreadAsMessageThread();
}

void JuceRuntime::pump() noexcept
{
    constexpr int kMaxMessagesPerPump = 256; // bounded: a timer storm must not hold the host's thread
   #if JUCE_MAC || JUCE_IOS
    // The toolkit posts into the run loop of the thread that started it (a source in the common modes). Without the host's loop on this thread nobody runs it: take the ready sources, once each.
    for (int i = 0; i < kMaxMessagesPerPump; ++i)
        if (CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.0, true) != kCFRunLoopRunHandledSource)
            break;
   #elif JUCE_LINUX || JUCE_BSD || JUCE_WINDOWS
    for (int i = 0; i < kMaxMessagesPerPump; ++i)
    {
        bool dispatched = false;
        JUCE_TRY
        {
            dispatched = juce::detail::dispatchNextMessageOnSystemQueue (true);
        }
        JUCE_CATCH_EXCEPTION
        if (! dispatched)
            break;
    }
   #else
    (void) kMaxMessagesPerPump;
   #endif
}
} // namespace synthyboy::bitmap
