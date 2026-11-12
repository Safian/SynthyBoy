// SynthyBoy - one binary with JUCE 7 (not renamed) and SynthyBoy's static UI library (JUCE 8 in its own namespace, tests/ios-juce7/CMakeLists.txt).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// What the Kick app does on iOS, as plainly as possible: its own JUCE 7 runs; the plug-in is entered through synthyboy_clap_get_entry (); its GUI is a pixel buffer. The test passes when
//   - the binary links (no symbol defined twice although JUCE 7 is linked whole),
//   - JUCE 7 works before, while and after the other JUCE has a GUI and after it is gone,
//   - the GUI of SynthyBoy produces a frame of the right size, opaque, and answers a touch,
//   - the Objective-C runtime has nothing to say about a class that is implemented twice (stderr is read by the script that runs this: "implemented in both").

#include <CoreFoundation/CoreFoundation.h>
#include <clap/clap.h>

#include <cstdio>
#include <cstring>

#include "bitmap_gui_ext.h"

extern "C" const clap_plugin_entry_t* synthyboy_clap_get_entry (void);
int juce7StandInRound();
void juce7StandInShutdown();

namespace
{
int s_failures = 0;
void check (bool ok, const char* what)
{
    std::printf ("    %s: %s\n", ok ? "ok  " : "FAIL", what);
    if (! ok)
        ++s_failures;
}

/** What the Kick app's main thread does between two frames: its run loop runs (JUCE 7's message manager lives in it, and SynthyBoy's toolkit posts into it when the GUI is created with the flag). */
void runLoop (int milliseconds)
{
    CFRunLoopRunInMode (kCFRunLoopDefaultMode, milliseconds / 1000.0, false);
}

const clap_host_t s_host = { CLAP_VERSION, nullptr, "ios-juce7-stand-in", "Safian", "", "1.0.0",
                             [] (const clap_host_t*, const char*) -> const void* { return nullptr; }, [] (const clap_host_t*) {}, [] (const clap_host_t*) {}, [] (const clap_host_t*) {} };
} // namespace

int main()
{
    std::printf ("JUCE 7 first\n");
    check (juce7StandInRound() == 0, "JUCE 7: message manager, software painting, keyboard component, a timer");

    std::printf ("SynthyBoy's GUI next to it\n");
    const clap_plugin_entry_t* entry = synthyboy_clap_get_entry();
    check (entry != nullptr && entry->init ("(linked)"), "synthyboy_clap_get_entry () and init");
    if (entry == nullptr)
        return 1;
    const auto* factory = static_cast<const clap_plugin_factory_t*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID));
    check (factory != nullptr && factory->get_plugin_count (factory) == 1, "the plug-in factory");
    const clap_plugin_t* plugin = factory != nullptr ? factory->create_plugin (factory, &s_host, "com.safian.synthyboy") : nullptr;
    check (plugin != nullptr && plugin->init (plugin), "the plug-in");
    if (plugin == nullptr)
        return 1;
    const auto* gui = static_cast<const safian_plugin_bitmap_gui_t*> (plugin->get_extension (plugin, SAFIAN_EXT_BITMAP_GUI));
    check (gui != nullptr, "the bitmap GUI extension is there");
    if (gui == nullptr)
        return 1;

    check (gui->create (plugin, 390, 844, 2.0, SAFIAN_BITMAP_GUI_CREATE_HOST_RUNS_EVENT_LOOP), "create 390 x 844 at scale 2, the host runs the event loop");
    for (int i = 0; i < 40; ++i)
    {
        gui->tick (plugin);
        runLoop (5);
    }
    safian_bitmap_gui_frame_t frame {};
    check (gui->render (plugin, &frame) && frame.width == 780 && frame.height == 1688 && frame.pixels != nullptr, "a frame of 780 x 1688 device pixels");
    bool opaque = true;
    for (uint32_t y = 0; y < frame.height && opaque; y += 37)
        for (uint32_t x = 0; x < frame.width; x += 41)
            opaque = opaque && frame.pixels[static_cast<size_t> (y) * frame.stride_bytes + x * 4 + 3] == 255;
    check (opaque, "the frame is opaque");
    const uint64_t firstFrame = frame.frame_id;

    check (juce7StandInRound() == 0, "JUCE 7 again, while the other JUCE has a GUI");

    safian_bitmap_gui_pointer_event_t touch {};
    touch.type = SAFIAN_BITMAP_GUI_POINTER_DOWN;
    touch.kind = SAFIAN_BITMAP_GUI_POINTER_KIND_TOUCH;
    touch.x = 119;
    touch.y = 128; // the FLT tab of the phone layout
    touch.buttons = SAFIAN_BITMAP_GUI_BUTTON_PRIMARY;
    gui->pointer_event (plugin, &touch);
    touch.type = SAFIAN_BITMAP_GUI_POINTER_UP;
    touch.buttons = 0;
    gui->pointer_event (plugin, &touch);
    for (int i = 0; i < 40; ++i)
    {
        gui->tick (plugin);
        runLoop (5);
    }
    check (gui->render (plugin, &frame) && frame.frame_id > firstFrame, "a touch changes the picture");

    gui->destroy (plugin);
    check (juce7StandInRound() == 0, "JUCE 7 after the GUI of SynthyBoy is gone");
    plugin->destroy (plugin);
    entry->deinit();
    check (juce7StandInRound() == 0, "JUCE 7 after the module is shut down");
    juce7StandInShutdown();

    std::printf (s_failures == 0 ? "ALL OK\n" : "%d FAILED\n", s_failures);
    return s_failures == 0 ? 0 : 1;
}
