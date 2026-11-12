// SynthyBoy - tests of the CLAP extension com.safian.bitmap-gui/1 against the built .clap (a host without JUCE: tests/bitmap-gui/bitmap_gui_harness.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cstdio>
#include <filesystem>
#include <fstream>

#include "bitmap_gui_harness.h"
#include "png_writer.h"

#include <clap/ext/remote-controls.h>
#include <clap/ext/state.h>
#include "test_runner.h"
#include "synthyboy/preferences.h"
#include "synthyboy/preset.h"
#include "synthyboy_content_ext.h"

using namespace bgtest;

namespace
{
bool fullFrameRect (const FrameInfo& f, const Image& img)
{
    return f.dirty.size() == 1 && f.dirty[0].x == 0 && f.dirty[0].y == 0 && f.dirty[0].w == img.width && f.dirty[0].h == img.height;
}
} // namespace

TEST (bg_module_loads_and_offers_the_extension)
{
    Instance host;
    if (! CHECK (host.ok()))
        return;
    safian_bitmap_gui_info_t info {};
    CHECK (host.gui->get_info (host.plugin, &info));
    CHECK_EQ (info.pixel_format, static_cast<uint32_t> (SAFIAN_BITMAP_GUI_PIXEL_BGRA8888_PREMUL));
    CHECK (info.max_dirty_rects >= 1);
    CHECK (info.min_scale <= 1.0 && info.max_scale >= 2.0);
    CHECK ((info.flags & SAFIAN_BITMAP_GUI_CAP_MULTITOUCH) != 0);
    uint32_t w = 0, h = 0;
    CHECK (host.gui->get_preferred_size (host.plugin, &w, &h));
    CHECK (w >= 360 && h >= 480);
    CHECK (host.gui->can_resize (host.plugin));
    safian_bitmap_gui_size_limits_t limits {};
    CHECK (host.gui->get_size_limits (host.plugin, &limits));
    CHECK (limits.min_width <= w && w <= limits.max_width && limits.min_height <= h && h <= limits.max_height);
}

TEST (bg_everything_is_harmless_without_a_gui)
{
    Instance host;
    if (! CHECK (host.ok()))
        return;
    FrameInfo f;
    uint32_t w = 0, h = 0;
    CHECK (! host.gui->get_size (host.plugin, &w, &h));
    CHECK (! host.gui->set_size (host.plugin, 800, 600));
    CHECK (! host.gui->set_scale (host.plugin, 2.0));
    CHECK (! host.gui->tick (host.plugin));
    CHECK (! host.gui->is_dirty (host.plugin));
    CHECK (! host.render (f));
    safian_bitmap_gui_pointer_event_t pe {};
    pe.type = SAFIAN_BITMAP_GUI_POINTER_DOWN;
    CHECK (! host.gui->pointer_event (host.plugin, &pe));
    CHECK (! host.gui->text_input (host.plugin, "x"));
    host.gui->set_focus (host.plugin, true);
    host.gui->request_done (host.plugin, 1, SAFIAN_BITMAP_GUI_RESULT_OK, "/x");
    host.destroyGui();
    host.destroyGui();
}

TEST (bg_gui_works_before_activate_and_creates_once)
{
    Instance host; // not activated: no audio engine running, no clap_plugin::activate () yet
    if (! CHECK (host.ok()))
        return;
    CHECK (host.create (1024, 768, 1.0));
    CHECK (! host.create (800, 600, 1.0)); // one GUI per instance
    uint32_t w = 0, h = 0;
    CHECK (host.gui->get_size (host.plugin, &w, &h));
    CHECK_EQ (w, 1024u);
    CHECK_EQ (h, 768u);
    FrameInfo f;
    Image img;
    CHECK (host.render (f, &img));
    CHECK_EQ (img.width, 1024u);
    CHECK_EQ (img.height, 768u);
    host.destroyGui();
    host.destroyGui(); // harmless twice
    CHECK (host.create (390, 844, 2.0)); // and again after a destroy, now at scale 2
    CHECK (host.render (f, &img));
    CHECK_EQ (img.width, 780u);
    CHECK_EQ (img.height, 1688u);
}

TEST (bg_first_frame_is_full_opaque_bgra)
{
    Instance host;
    if (! CHECK (host.ok()))
        return;
    CHECK (host.create (1024, 768, 1.0));
    FrameInfo f;
    Image img;
    CHECK (host.render (f, &img));
    CHECK_EQ (f.id, 1u);
    CHECK_EQ (f.format, static_cast<uint32_t> (SAFIAN_BITMAP_GUI_PIXEL_BGRA8888_PREMUL));
    CHECK ((f.flags & SAFIAN_BITMAP_GUI_FRAME_FULL) != 0);
    CHECK (fullFrameRect (f, img));
    CHECK (f.stride >= img.width * 4 && f.stride % 4 == 0);
    uint64_t opaque = 0, distinct = 0;
    uint32_t first = img.pixels[0];
    for (uint32_t p : img.pixels)
    {
        opaque += (p >> 24) == 0xFF;
        distinct += p != first;
    }
    CHECK_EQ (opaque, static_cast<uint64_t> (img.pixels.size())); // every pixel opaque: premultiplied and straight alpha are the same bytes
    CHECK (distinct > img.pixels.size() / 10);                      // there is a GUI in there, not a flat colour
    if (const char* dir = std::getenv ("SYNTHYBOY_BITMAP_GUI_DUMP_DIR"))
        writePng (std::string (dir) + "/first-frame-1024x768.png", img);
}

namespace
{
/** Renders until the GUI is idle (the first frame and what follows it), returns the last image. */
Image settle (Instance& host, int milliseconds = 500)
{
    host.tickFor (milliseconds);
    FrameInfo f;
    Image img;
    host.render (f, &img);
    host.render (f, &img);
    return img;
}

/** The centre of the first point on a grid in `area` whose drag (up by 40) changes the parameter `key`. The GUI is probed like a user would: by touching it. */
bool findKnob (Instance& host, const char* key, Rect area, double& foundX, double& foundY)
{
    const double before = host.param (key);
    for (int y = area.y; y < area.y + static_cast<int> (area.h); y += 10)
        for (int x = area.x; x < area.x + static_cast<int> (area.w); x += 10)
        {
            host.drag (x, y, 0, -30, 4);
            host.processBlock (64);
            if (std::fabs (host.param (key) - before) > 1.0e-6)
            {
                foundX = x;
                foundY = y;
                return true;
            }
        }
    return false;
}

const Rect kOsc1Level { 560, 470, 70, 60 }; // the "Level" knob of oscillator 1 on the first page at 1024 x 768 (the probe scans this area)
} // namespace

/** A host with hardware keys (the MPC) passes CREATE_NO_ONSCREEN_KEYBOARD: the piano strip (white keys) is not drawn and its area goes to the panels. */
TEST (bg_no_onscreen_keyboard_flag_hides_the_piano_strip)
{
    auto whiteInBottom = [] (uint32_t flags) -> uint64_t
    {
        Instance host;
        if (! host.ok() || ! host.create (1024, 600, 1.0, flags))
            return ~uint64_t { 0 };
        FrameInfo f;
        Image img;
        if (! host.render (f, &img))
            return ~uint64_t { 0 };
        uint64_t white = 0;
        for (uint32_t y = img.height - 70; y < img.height; ++y)
            for (uint32_t x = 0; x < img.width; ++x)
            {
                const uint32_t p = img.pixels[static_cast<size_t> (y) * img.width + x];
                white += (p & 0xFF) > 0xE0 && ((p >> 8) & 0xFF) > 0xE0 && ((p >> 16) & 0xFF) > 0xE0;
            }
        return white;
    };
    const auto withKeys = whiteInBottom (0);
    const auto without = whiteInBottom (SAFIAN_BITMAP_GUI_CREATE_NO_ONSCREEN_KEYBOARD);
    CHECK (withKeys != ~uint64_t { 0 } && without != ~uint64_t { 0 });
    CHECK (withKeys > 5000);        // the white keys are there by default
    CHECK (without < withKeys / 10); // and gone with the flag
}

TEST (bg_idle_gui_produces_no_pixels)
{
    Instance host;
    if (! CHECK (host.ok()))
        return;
    CHECK (host.create (1024, 768, 1.0));
    FrameInfo f;
    Image img;
    CHECK (host.render (f, &img));
    host.tickFor (700);
    CHECK (host.render (f, &img)); // whatever settled after the first frame
    const uint64_t firstId = f.id;
    // an idle GUI: nothing is dirty, render () returns the same frame again (same id, no rectangles), the cost is the flag
    CHECK (! host.gui->is_dirty (host.plugin));
    for (int i = 0; i < 20; ++i)
    {
        CHECK (host.render (f, &img));
        CHECK_EQ (f.dirty.size(), 0u);
        CHECK_EQ (f.id, firstId); // unchanged frame: the same id
        host.tickFor (10);
    }
    uint64_t total = 0;
    int dirtyFrames = 0;
    const uint64_t idBefore = f.id;
    const auto t0 = Clock::now();
    for (int i = 0; i < 1000; ++i)
    {
        host.gui->tick (host.plugin);
        host.gui->is_dirty (host.plugin);
    }
    const double perCallUs = std::chrono::duration<double, std::micro> (Clock::now() - t0).count() / 1000.0;
    CHECK (perCallUs < 500.0 * timeScale()); // generous: an idle tick () + is_dirty () is microseconds
    for (int i = 0; i < 30; ++i)
    {
        host.render (f);
        if (std::getenv ("SYNTHYBOY_BITMAP_GUI_DEBUG") && ! f.dirty.empty())
        {
            std::printf ("    frame %llu:", static_cast<unsigned long long> (f.id));
            for (auto& r : f.dirty)
                std::printf (" [%d,%d %ux%u]", r.x, r.y, r.w, r.h);
            std::printf ("\n");
        }
        total += f.dirtyArea();
        dirtyFrames += ! f.dirty.empty();
        host.tickFor (15);
    }
    CHECK_EQ (f.id - idBefore, static_cast<uint64_t> (dirtyFrames)); // the frame id counts exactly the frames that changed pixels
    CHECK (total < 30ull * 1024 * 768 / 20);
    std::printf ("    idle: %d of 30 frames changed, tick+is_dirty %.1f us\n", dirtyFrames, perCallUs);
}

TEST (bg_drag_on_a_knob_changes_the_clap_parameter_and_the_audio)
{
    Instance host;
    if (! CHECK (host.ok()))
        return;
    host.activate();
    CHECK (host.create (1024, 768, 1.0));
    settle (host);

    double x = 0, y = 0;
    const double defaultLevel = host.param ("osc1.level");
    if (! CHECK (findKnob (host, "osc1.level", kOsc1Level, x, y)))
        return;
    host.processBlock (64);
    host.queueParam ("osc1.level", defaultLevel);
    host.processBlock (64);
    {
        std::lock_guard<std::mutex> g (host.events.lock);
        host.events.paramOut.clear();
    }

    // loud reference: the same note at the default level
    host.queueNote (true, 60);
    const double loud = host.rms (6000);
    host.queueNote (false, 60);
    host.rms (30000); // release tail
    // a drag down on the knob: gesture begin, values, gesture end reach the host as CLAP events; the parameter and the sound follow
    host.drag (x, y, 0, 70, 10);
    host.processBlock (64);
    const auto id = synthyboy::findParamByKey ("osc1.level")->id;
    int begins = 0, ends = 0, values = 0;
    {
        std::lock_guard<std::mutex> g (host.events.lock);
        for (const auto& e : host.events.paramOut)
            if (e.id == id)
            {
                begins += e.type == CLAP_EVENT_PARAM_GESTURE_BEGIN;
                ends += e.type == CLAP_EVENT_PARAM_GESTURE_END;
                values += e.type == CLAP_EVENT_PARAM_VALUE;
            }
    }
    CHECK_EQ (begins, 1);
    CHECK_EQ (ends, 1);
    CHECK (values >= 1);
    const double lowered = host.param ("osc1.level");
    CHECK (lowered < defaultLevel - 0.1);
    host.queueNote (true, 60);
    const double quiet = host.rms (6000);
    CHECK (quiet < loud * 0.8); // the own engine (iOS) acts on osc1.level since the parity work: the sound follows on every engine
    std::printf ("    osc1.level %.3f -> %.3f, rms %.4f -> %.4f\n", defaultLevel, lowered, loud, quiet);
}

TEST (bg_external_parameter_change_refreshes_the_pixels)
{
    Instance host;
    if (! CHECK (host.ok()))
        return;
    host.activate();
    CHECK (host.create (1024, 768, 1.0));
    Image before = settle (host);
    // host automation: the value arrives in the audio callback, the GUI takes it at its next tick
    host.queueParam ("osc1.level", 0.95);
    host.queueParam ("osc1.frame", 0.7);
    host.processBlock (128);
    CHECK (host.gui->tick (host.plugin));
    FrameInfo f;
    Image after;
    CHECK (host.render (f, &after));
    CHECK (! f.dirty.empty());
    CHECK ((f.flags & SAFIAN_BITMAP_GUI_FRAME_FULL) == 0);
    CHECK (differingPixels (before, after) > 100);
    // only the controls that show the value (and the wavetable view) repaint, not the screen
    CHECK (f.dirtyArea() < 1024u * 768u / 3);
    for (const auto& r : f.dirty)
        CHECK (r.x >= 0 && r.y >= 0 && r.x + static_cast<int64_t> (r.w) <= 1024 && r.y + static_cast<int64_t> (r.h) <= 768);
    // outside every dirty rectangle the pixels are exactly the old ones
    uint64_t outside = 0;
    for (uint32_t py = 0; py < after.height; ++py)
        for (uint32_t px = 0; px < after.width; ++px)
        {
            bool inside = false;
            for (const auto& r : f.dirty)
                inside = inside || (static_cast<int64_t> (px) >= r.x && static_cast<int64_t> (px) < r.x + static_cast<int64_t> (r.w) && static_cast<int64_t> (py) >= r.y
                                    && static_cast<int64_t> (py) < r.y + static_cast<int64_t> (r.h));
            outside += (! inside && before.at (px, py) != after.at (px, py));
        }
    CHECK_EQ (outside, 0u);
}

TEST (bg_resize_and_scale)
{
    Instance host;
    if (! CHECK (host.ok()))
        return;
    CHECK (host.create (1024, 768, 1.0));
    FrameInfo f;
    Image img;
    host.render (f, &img);
    CHECK (host.gui->set_size (host.plugin, 800, 600));
    uint32_t w = 0, h = 0;
    CHECK (host.gui->get_size (host.plugin, &w, &h));
    CHECK_EQ (w, 800u);
    CHECK_EQ (h, 600u);
    CHECK (host.gui->is_dirty (host.plugin));
    CHECK (host.render (f, &img));
    CHECK_EQ (img.width, 800u);
    CHECK_EQ (img.height, 600u);
    CHECK ((f.flags & SAFIAN_BITMAP_GUI_FRAME_FULL) != 0);
    CHECK (fullFrameRect (f, img));
    // below the limits: clamped, and the answer is what get_size reports
    CHECK (host.gui->set_size (host.plugin, 100, 100));
    CHECK (host.gui->get_size (host.plugin, &w, &h));
    CHECK_EQ (w, 360u);
    CHECK_EQ (h, 480u);
    CHECK (host.gui->set_size (host.plugin, 5000, 5000));
    CHECK (host.gui->get_size (host.plugin, &w, &h));
    CHECK_EQ (w, 1800u);
    CHECK_EQ (h, 1400u);
    CHECK (host.gui->set_size (host.plugin, 1024, 600));
    CHECK (host.gui->set_scale (host.plugin, 1.5));
    CHECK (host.render (f, &img));
    CHECK_EQ (img.width, 1536u);
    CHECK_EQ (img.height, 900u);
    CHECK ((f.flags & SAFIAN_BITMAP_GUI_FRAME_FULL) != 0);
    CHECK (host.gui->set_scale (host.plugin, 0.01)); // clamped to the smallest scale
    CHECK (host.render (f, &img));
    CHECK_EQ (img.width, 512u);
    CHECK_EQ (img.height, 300u);
    // the layout follows the width: the same GUI at 390 is the phone layout, at 1024 the wide one
    CHECK (host.gui->set_scale (host.plugin, 1.0));
    CHECK (host.gui->set_size (host.plugin, 390, 844));
    Image phone;
    CHECK (host.render (f, &phone));
    CHECK (host.gui->set_size (host.plugin, 1024, 768));
    Image wide;
    CHECK (host.render (f, &wide));
    CHECK (differingPixels (phone, wide) > 1000);
}

TEST (bg_wheel_turns_a_knob_and_keys_undo)
{
    Instance host;
    if (! CHECK (host.ok()))
        return;
    host.activate();
    CHECK (host.create (1024, 768, 1.0));
    host.gui->set_focus (host.plugin, true);
    settle (host);
    const double before = host.param ("osc1.level");
    safian_bitmap_gui_wheel_event_t wheel {};
    wheel.x = 587;
    wheel.y = 498;
    wheel.delta_y = 3.0;
    CHECK (host.gui->wheel_event (host.plugin, &wheel));
    host.processBlock (64);
    const double after = host.param ("osc1.level");
    CHECK (after > before);
    // Ctrl + Z: the editor's undo puts the value back (through the parameter events, like any edit)
    host.key ('z', "z", SAFIAN_BITMAP_GUI_MOD_CTRL);
    host.processBlock (64);
    CHECK_NEAR (host.param ("osc1.level"), before, 1.0e-3);
    host.key ('z', "Z", SAFIAN_BITMAP_GUI_MOD_CTRL | SAFIAN_BITMAP_GUI_MOD_SHIFT); // redo
    host.processBlock (64);
    CHECK_NEAR (host.param ("osc1.level"), after, 1.0e-3);
}

TEST (bg_two_fingers_are_a_gesture_not_two_drags)
{
    Instance host;
    if (! CHECK (host.ok()))
        return;
    host.activate();
    CHECK (host.create (1024, 768, 1.0));
    settle (host);
    const uint32_t touch = SAFIAN_BITMAP_GUI_POINTER_KIND_TOUCH;
    const double start = host.param ("osc1.level");
    host.pointer (SAFIAN_BITMAP_GUI_POINTER_DOWN, 587, 498, 1, touch);
    for (int i = 1; i <= 5; ++i)
        host.pointer (SAFIAN_BITMAP_GUI_POINTER_MOVE, 587, 498 - 3 * i, 1, touch);
    host.processBlock (64);
    const double oneFinger = host.param ("osc1.level");
    CHECK (oneFinger > start); // the first finger drags the knob
    // a second finger lands somewhere else: the pair is a gesture, the knob does not move any more
    host.pointer (SAFIAN_BITMAP_GUI_POINTER_DOWN, 800, 700, 2, touch);
    for (int i = 1; i <= 8; ++i)
    {
        host.pointer (SAFIAN_BITMAP_GUI_POINTER_MOVE, 587, 483 - 6 * i, 1, touch);
        host.pointer (SAFIAN_BITMAP_GUI_POINTER_MOVE, 800 + 5 * i, 700, 2, touch);
        host.gui->tick (host.plugin);
    }
    host.pointer (SAFIAN_BITMAP_GUI_POINTER_UP, 587, 440, 1, touch);
    host.pointer (SAFIAN_BITMAP_GUI_POINTER_UP, 840, 700, 2, touch);
    host.processBlock (64);
    CHECK_NEAR (host.param ("osc1.level"), oneFinger, 1.0e-6);
    // and the fingers are free again: a new single finger drags
    host.drag (587, 498, 0, -20, 4, 3);
    host.processBlock (64);
    CHECK (host.param ("osc1.level") > oneFinger);
}

namespace
{
/** The preferences file of the test's user directory with "Double-click params: open the value box"; removed again at the end. */
struct ValueBoxPreference
{
    ValueBoxPreference()
    {
        const char* dir = std::getenv ("SYNTHYBOY_USER_DIR");
        userDir = dir != nullptr ? dir : "";
        std::filesystem::create_directories (userDir);
        synthyboy::Preferences prefs;
        prefs.doubleClickResets = false;
        synthyboy::savePreferences (userDir, prefs);
    }
    ~ValueBoxPreference() { std::filesystem::remove (synthyboy::preferencesPath (userDir)); }
    std::string userDir;
};
} // namespace

TEST (bg_double_tap_opens_the_value_box_and_the_host_keyboard_types_into_it)
{
    if (! foregroundForOverlays())
    {
        std::printf ("    SKIPPED: this process cannot become the active application (no window server session): call-outs close at once\n");
        return;
    }
    ValueBoxPreference preference;
    Instance host;
    if (! CHECK (host.ok()))
        return;
    host.activate();
    CHECK (host.create (1024, 768, 1.0));
    host.gui->set_focus (host.plugin, true);
    settle (host);
    CHECK (host.events.textInput.empty());
    // two taps on a knob: its value sheet opens with the exact value entry focused (a preference of the user), so the host is asked for its keyboard
    host.tap (587, 498, 1);
    host.tap (587, 498, 1);
    host.tickFor (400);
    FrameInfo f;
    Image sheet;
    CHECK (host.render (f, &sheet));
    if (const char* dir = std::getenv ("SYNTHYBOY_BITMAP_GUI_DUMP_DIR"))
        writePng (std::string (dir) + "/value-box.png", sheet);
    CHECK (! host.events.textInput.empty());
    if (host.events.textInput.empty())
        return;
    const auto& request = host.events.textInput.back();
    CHECK (request.show);
    CHECK (request.field.w > 0 && request.field.h > 0);
    CHECK (request.field.x >= 0 && request.field.y >= 0 && request.field.x + static_cast<int> (request.field.w) <= 1024);
    std::printf ("    text input: field [%d,%d %ux%u] kind %u text '%s'\n", request.field.x, request.field.y, request.field.w, request.field.h, request.kind, request.text.c_str());
    // the virtual keyboard commits "20", Enter applies it
    for (int i = 0; i < 8; ++i)
        host.key (SAFIAN_BITMAP_GUI_KEY_BACKSPACE); // an existing text goes first
    CHECK (host.gui->text_input (host.plugin, "20"));
    host.key (SAFIAN_BITMAP_GUI_KEY_ENTER);
    host.tickFor (200);
    host.processBlock (64);
    CHECK_NEAR (host.param ("osc1.level"), 20.0, 0.5);
    host.tap (30, 740); // a touch outside the sheet closes it (and is consumed by that)
    host.tickFor (300);
    CHECK (! host.events.textInput.back().show); // the field is gone: the host hides its keyboard
    CHECK (! host.gui->text_input (host.plugin, "x")); // without a text field nothing takes text
}

namespace
{
/** A 16 bit mono WAV file of a sine, written with the C library only. */
std::string writeSineWav (const std::string& path, double seconds = 0.25)
{
    const uint32_t rate = 48000, frames = static_cast<uint32_t> (rate * seconds), bytes = frames * 2;
    std::vector<uint8_t> wav;
    const auto put32 = [&] (uint32_t v) { for (int s = 0; s < 32; s += 8) wav.push_back (static_cast<uint8_t> (v >> s)); };
    const auto put16 = [&] (uint16_t v) { wav.push_back (static_cast<uint8_t> (v)); wav.push_back (static_cast<uint8_t> (v >> 8)); };
    for (char c : std::string ("RIFF")) wav.push_back (static_cast<uint8_t> (c));
    put32 (36 + bytes);
    for (char c : std::string ("WAVEfmt ")) wav.push_back (static_cast<uint8_t> (c));
    put32 (16); put16 (1); put16 (1); put32 (rate); put32 (rate * 2); put16 (2); put16 (16);
    for (char c : std::string ("data")) wav.push_back (static_cast<uint8_t> (c));
    put32 (bytes);
    for (uint32_t i = 0; i < frames; ++i)
        put16 (static_cast<uint16_t> (static_cast<int16_t> (12000.0 * std::sin (6.2831853 * 440.0 * i / rate))));
    if (FILE* f = std::fopen (path.c_str(), "wb"))
    {
        std::fwrite (wav.data(), 1, wav.size(), f);
        std::fclose (f);
    }
    return path;
}

/** Taps a grid over `area` until the host has been asked for a file; false when nothing asked. */
bool tapUntilFileRequested (Instance& host, Rect area)
{
    for (int y = area.y; y < area.y + static_cast<int> (area.h); y += 12)
        for (int x = area.x; x < area.x + static_cast<int> (area.w); x += 12)
        {
            host.tap (x, y);
            host.gui->tick (host.plugin);
            if (! host.events.fileRequests.empty())
                return true;
        }
    return false;
}
} // namespace

TEST (bg_the_load_button_asks_the_host_for_a_file_and_imports_the_answer)
{
    Instance host;
    if (! CHECK (host.ok()))
        return;
    host.activate();
    CHECK (host.create (1024, 768, 1.0));
    settle (host);
    const auto* content = static_cast<const synthyboy_plugin_content_t*> (host.plugin->get_extension (host.plugin, SYNTHYBOY_EXT_CONTENT));
    if (! CHECK (content != nullptr))
        return;
    // oscillator 1 becomes a sample oscillator (a host automation event): its view shows the Load button
    host.queueParam ("osc1.type", synthyboy::kOscTypeSample);
    host.processBlock (64);
    host.tickFor (200);
    FrameInfo f;
    host.render (f);
    CHECK (tapUntilFileRequested (host, { 352, 168, 312, 180 }));
    if (host.events.fileRequests.empty())
        return;
    const auto request = host.events.fileRequests.front();
    std::printf ("    request %u: mode %u title '%s' wildcards '%s' purpose '%s'\n", request.id, request.request.mode, request.title.c_str(), request.wildcards.c_str(), request.purpose.c_str());
    CHECK_EQ (request.request.mode, static_cast<uint32_t> (SAFIAN_BITMAP_GUI_FILE_OPEN));
    CHECK (request.wildcards.find ("*.wav") != std::string::npos);
    CHECK_EQ (request.purpose, std::string ("sample"));
    // the user cancels: nothing is imported
    const uint32_t samplesBefore = content->count (host.plugin, SYNTHYBOY_CONTENT_SAMPLE);
    host.gui->request_done (host.plugin, request.id, SAFIAN_BITMAP_GUI_RESULT_CANCELLED, nullptr);
    host.tickFor (300);
    CHECK_EQ (content->count (host.plugin, SYNTHYBOY_CONTENT_SAMPLE), samplesBefore);
    // an answer that nobody asked for is ignored
    host.gui->request_done (host.plugin, request.id, SAFIAN_BITMAP_GUI_RESULT_OK, "/does/not/matter");
    host.gui->request_done (host.plugin, 424242, SAFIAN_BITMAP_GUI_RESULT_OK, "/does/not/matter");
    host.tickFor (100);
    CHECK_EQ (content->count (host.plugin, SYNTHYBOY_CONTENT_SAMPLE), samplesBefore);
    // ask again and answer with a real file: it is decoded by the GUI, stored in the library and attached
    host.events.fileRequests.clear();
    CHECK (tapUntilFileRequested (host, { 352, 168, 312, 180 }));
    if (host.events.fileRequests.empty())
        return;
    const std::string wav = writeSineWav ((std::filesystem::temp_directory_path() / "synthyboy-bitmap-gui-test.wav").string());
    host.gui->request_done (host.plugin, host.events.fileRequests.back().id, SAFIAN_BITMAP_GUI_RESULT_OK, wav.c_str());
    bool imported = false;
    for (int i = 0; i < 150 && ! imported; ++i)
    {
        host.tickFor (20);
        imported = content->count (host.plugin, SYNTHYBOY_CONTENT_SAMPLE) > samplesBefore;
    }
    CHECK (imported);
    std::filesystem::remove (wav);
}

TEST (bg_a_host_that_cannot_ask_for_files_is_never_asked_and_nothing_breaks)
{
    Instance host (true, false); // the host extension without request_file / share_file
    if (! CHECK (host.ok()))
        return;
    host.activate();
    CHECK (host.create (1024, 768, 1.0));
    settle (host);
    host.queueParam ("osc1.type", synthyboy::kOscTypeSample);
    host.processBlock (64);
    host.tickFor (200);
    CHECK (! tapUntilFileRequested (host, { 352, 168, 312, 180 }));
    host.tickFor (100);
    FrameInfo f;
    CHECK (host.render (f));
}

TEST (bg_remote_controls_page_follows_the_panel_and_back)
{
    Instance host;
    if (! CHECK (host.ok()))
        return;
    host.activate();
    CHECK (host.create (1024, 768, 1.0));
    settle (host);
    const auto* remote = static_cast<const clap_plugin_remote_controls_t*> (host.plugin->get_extension (host.plugin, CLAP_EXT_REMOTE_CONTROLS));
    if (! CHECK (remote != nullptr))
        return;
    const auto pageHolding = [&] (const char* key) -> int
    {
        const clap_id id = synthyboy::findParamByKey (key)->id;
        for (uint32_t i = 0; i < remote->count (host.plugin); ++i)
        {
            clap_remote_controls_page_t page {};
            remote->get (host.plugin, i, &page);
            for (uint32_t k = 0; k < CLAP_REMOTE_CONTROLS_COUNT; ++k)
                if (page.param_ids[k] == id)
                    return static_cast<int> (page.page_id);
        }
        return -1;
    };
    host.tickFor (100);
    CHECK (! host.events.remotePages.empty()); // the OSC tab is shown from the start: its page was announced
    host.events.remotePages.clear();
    host.tap (320, 86); // the FLT tab
    host.tickFor (100);
    if (! CHECK (! host.events.remotePages.empty()))
        return;
    const int filterPage = pageHolding ("filter1.cutoff");
    CHECK (filterPage >= 0);
    std::printf ("    FLT tab -> remote page %u (page of filter1.cutoff: %d)\n", host.events.remotePages.back(), filterPage);
    CHECK (host.events.remotePages.back() < remote->count (host.plugin));
    // the host's bank moved to the page of osc1.level: the GUI shows the OSC panels again, and does not echo it
    const int oscPage = pageHolding ("osc1.level");
    CHECK (oscPage >= 0);
    host.events.remotePages.clear();
    FrameInfo f;
    Image flt, osc;
    host.render (f, &flt);
    CHECK (host.gui->show_remote_page (host.plugin, static_cast<clap_id> (oscPage)));
    host.tickFor (100);
    host.render (f, &osc);
    CHECK (differingPixels (flt, osc) > 5000);
    CHECK (host.events.remotePages.empty());
    CHECK (! host.gui->show_remote_page (host.plugin, 99999));
}

TEST (bg_host_without_the_host_extension_degrades)
{
    Instance host (false); // no com.safian.bitmap-gui.host/1 at all
    if (! CHECK (host.ok()))
        return;
    host.activate();
    CHECK (host.create (1024, 768, 1.0));
    settle (host);
    const double before = host.param ("osc1.level");
    host.drag (587, 498, 0, -30, 4);
    host.processBlock (64);
    CHECK (host.param ("osc1.level") > before);
    host.pointer (SAFIAN_BITMAP_GUI_POINTER_HOVER, 400, 300);
    host.pointer (SAFIAN_BITMAP_GUI_POINTER_DOWN, 587, 498, 0, SAFIAN_BITMAP_GUI_POINTER_KIND_TOUCH);
    host.tickFor (700); // a long press opens the sheet; there is nobody to ask for a keyboard
    host.pointer (SAFIAN_BITMAP_GUI_POINTER_UP, 587, 498, 0, SAFIAN_BITMAP_GUI_POINTER_KIND_TOUCH);
    host.tickFor (100);
    host.key (SAFIAN_BITMAP_GUI_KEY_ESCAPE);
    FrameInfo f;
    CHECK (host.render (f));
    CHECK_EQ (host.events.renderRequests + host.events.cursorChanges + static_cast<int> (host.events.textInput.size()), 0);
    host.destroyGui();
}

TEST (bg_hover_sets_the_cursor_and_the_host_is_asked_to_render_once)
{
    Instance host;
    if (! CHECK (host.ok()))
        return;
    CHECK (host.create (1024, 768, 1.0));
    settle (host);
    host.events.renderRequests = 0;
    host.pointer (SAFIAN_BITMAP_GUI_POINTER_HOVER, 587, 498); // over a knob
    host.pointer (SAFIAN_BITMAP_GUI_POINTER_HOVER, 590, 500);
    host.pointer (SAFIAN_BITMAP_GUI_POINTER_HOVER, 20, 20);
    CHECK (host.events.cursorChanges >= 1);
    // request_render comes when the first change after a render happens, not for every repaint
    host.pointer (SAFIAN_BITMAP_GUI_POINTER_DOWN, 587, 498);
    host.pointer (SAFIAN_BITMAP_GUI_POINTER_MOVE, 587, 480);
    host.pointer (SAFIAN_BITMAP_GUI_POINTER_MOVE, 587, 470);
    host.pointer (SAFIAN_BITMAP_GUI_POINTER_UP, 587, 470);
    CHECK_EQ (host.events.renderRequests, 1);
    FrameInfo f;
    host.render (f);
    host.pointer (SAFIAN_BITMAP_GUI_POINTER_LEAVE, 0, 0);
    host.tickFor (50);
    CHECK (host.events.renderRequests >= 1);
}

TEST (bg_two_instances_are_independent_and_destroyed_in_any_order)
{
    auto a = std::make_unique<Instance>();
    auto b = std::make_unique<Instance>();
    if (! CHECK (a->ok() && b->ok()))
        return;
    a->activate();
    b->activate();
    CHECK (a->create (1024, 768, 1.0));
    CHECK (b->create (390, 844, 1.0));
    settle (*a);
    settle (*b);
    const double aBefore = a->param ("osc1.level"), bBefore = b->param ("osc1.level");
    a->drag (587, 498, 0, -40, 6);
    a->processBlock (64);
    b->processBlock (64);
    CHECK (a->param ("osc1.level") > aBefore);
    CHECK_EQ (b->param ("osc1.level"), bBefore);
    FrameInfo fa, fb;
    Image ia, ib;
    CHECK (a->render (fa, &ia));
    CHECK (b->render (fb, &ib));
    CHECK_EQ (ia.width, 1024u);
    CHECK_EQ (ib.width, 390u);
    a.reset(); // the first one goes while the second one lives on
    b->drag (200, 400, 0, -20, 4);
    b->tickFor (50);
    CHECK (b->render (fb, &ib));
    b.reset();
    // and the toolkit starts again for the next GUI
    Instance c;
    CHECK (c.create (1024, 600, 1.0));
    CHECK (c.render (fb, &ib));
}

TEST (bg_animated_views_advance_while_a_note_sounds_and_settle_afterwards)
{
    Instance host;
    if (! CHECK (host.ok()))
        return;
    host.activate();
    CHECK (host.create (1024, 768, 1.0));
    settle (host);
    host.startAudio();
    host.queueNote (true, 60);
    FrameInfo f;
    Image img;
    int changedFrames = 0, needsAnother = 0;
    bool meterRepainted = false;
    const auto end = Clock::now() + std::chrono::milliseconds (scaled (900));
    while (Clock::now() < end)
    {
        host.render (f, &img);
        changedFrames += ! f.dirty.empty();
        needsAnother += (f.flags & SAFIAN_BITMAP_GUI_FRAME_NEEDS_ANOTHER) != 0;
        for (const auto& r : f.dirty)
            meterRepainted = meterRepainted || (r.x + static_cast<int64_t> (r.w) > 1000 && r.y < 64); // the MAIN meter
        sleepMs (16);
    }
    CHECK (changedFrames >= 5);   // meters and voice counter move while the sound plays
    CHECK (needsAnother >= 1);    // and the frames say so
    CHECK (meterRepainted);
    host.queueNote (false, 60);
    host.tickFor (3500); // the release tail, then the meter falls to silence
    host.render (f, &img);
    host.tickFor (300);
    host.render (f, &img);
    CHECK (f.dirty.empty());
    CHECK ((f.flags & SAFIAN_BITMAP_GUI_FRAME_NEEDS_ANOTHER) == 0); // idle again: no more frames asked for
    host.stopAudio();
    std::printf ("    %d frames changed in 900 ms of sound, %d asked for another\n", changedFrames, needsAnother);
}

TEST (bg_the_gui_works_on_the_audio_threads_heels_no_race)
{
    Instance host;
    if (! CHECK (host.ok()))
        return;
    host.activate();
    CHECK (host.create (1024, 768, 1.0));
    host.gui->set_focus (host.plugin, true);
    host.startAudio();
    host.queueNote (true, 55);
    FrameInfo f;
    Image img;
    const auto end = Clock::now() + std::chrono::milliseconds (scaled (1500));
    int round = 0;
    while (Clock::now() < end)
    {
        // the GUI edits, the host automates, notes come and go, the GUI renders; the audio thread runs the whole time (under TSan this is the test)
        host.drag (587 - (round % 3) * 5, 498, 0, (round % 2 == 0) ? -25 : 25, 4);
        host.queueParam ("filter1.cutoff", 2000.0 + 100.0 * (round % 7));
        host.gui->tick (host.plugin);
        host.render (f, &img);
        if (round % 5 == 0)
        {
            host.queueNote (false, 55);
            host.queueNote (true, 55 + round % 12);
        }
        host.tap (100 + 50.0 * (round % 5), 700); // the on-screen keyboard
        ++round;
        sleepMs (3);
    }
    host.stopAudio();
    CHECK (round > 20);
    CHECK (host.blocks.load() > 20);
    host.destroyGui();
}

TEST (bg_state_round_trip_keeps_what_the_gui_edited)
{
    Instance first, second;
    if (! CHECK (first.ok() && second.ok()))
        return;
    first.activate();
    second.activate();
    CHECK (first.create (1024, 768, 1.0));
    settle (first);
    first.drag (587, 498, 0, -50, 6);
    first.processBlock (64);
    const double edited = first.param ("osc1.level");
    CHECK (std::fabs (edited - 50.0) > 1.0);
    const auto* state = static_cast<const clap_plugin_state_t*> (first.plugin->get_extension (first.plugin, CLAP_EXT_STATE));
    const auto* state2 = static_cast<const clap_plugin_state_t*> (second.plugin->get_extension (second.plugin, CLAP_EXT_STATE));
    if (! CHECK (state != nullptr && state2 != nullptr))
        return;
    std::string saved;
    clap_ostream_t out {};
    out.ctx = &saved;
    out.write = [] (const clap_ostream_t* s, const void* buffer, uint64_t size) -> int64_t
    {
        static_cast<std::string*> (s->ctx)->append (static_cast<const char*> (buffer), static_cast<size_t> (size));
        return static_cast<int64_t> (size);
    };
    CHECK (state->save (first.plugin, &out));
    CHECK (! saved.empty());
    struct Reader
    {
        const std::string* text;
        size_t at = 0;
    } reader { &saved };
    clap_istream_t in {};
    in.ctx = &reader;
    in.read = [] (const clap_istream_t* s, void* buffer, uint64_t size) -> int64_t
    {
        auto* r = static_cast<Reader*> (s->ctx);
        const size_t n = std::min<size_t> (size, r->text->size() - r->at);
        std::memcpy (buffer, r->text->data() + r->at, n);
        r->at += n;
        return static_cast<int64_t> (n);
    };
    CHECK (state2->load (second.plugin, &in));
    CHECK_NEAR (second.param ("osc1.level"), edited, 1.0e-3);
    // a GUI created after the load shows the loaded value (the second instance's knob reads the same)
    CHECK (second.create (1024, 768, 1.0));
    settle (second);
    FrameInfo f;
    Image a, b;
    first.render (f, &a);
    second.render (f, &b);
    const Rect panels { 0, 64, 1024, 704 }; // below the top bar (the undo button there is lit in the first instance only)
    CHECK_EQ (differingPixels (a, b, &panels), 0u);
    if (differingPixels (a, b, &panels) != 0)
    {
        uint32_t x0 = 99999, y0 = 99999, x1 = 0, y1 = 0;
        for (uint32_t y = 0; y < a.height; ++y)
            for (uint32_t x = 0; x < a.width; ++x)
                if (a.at (x, y) != b.at (x, y))
                {
                    x0 = std::min (x0, x); y0 = std::min (y0, y); x1 = std::max (x1, x); y1 = std::max (y1, y);
                }
        std::printf ("    differing box [%u,%u .. %u,%u]\n", x0, y0, x1, y1);
        if (const char* dir = std::getenv ("SYNTHYBOY_BITMAP_GUI_DUMP_DIR"))
        {
            writePng (std::string (dir) + "/state-a.png", a);
            writePng (std::string (dir) + "/state-b.png", b);
        }
    }
}

TEST (bg_destroy_and_recreate_many_times)
{
    Instance host;
    if (! CHECK (host.ok()))
        return;
    host.activate();
    FrameInfo f;
    for (int i = 0; i < 12; ++i)
    {
        CHECK (host.create (i % 2 == 0 ? 1024 : 390, i % 2 == 0 ? 768 : 844, i % 3 == 0 ? 2.0 : 1.0));
        host.drag (200, 300, 0, -20, 3);
        host.pointer (SAFIAN_BITMAP_GUI_POINTER_DOWN, 100, 100, 7, SAFIAN_BITMAP_GUI_POINTER_KIND_TOUCH); // a finger still down when the GUI goes
        host.render (f);
        host.destroyGui();
    }
    CHECK (host.create (1024, 768, 1.0));
    CHECK (host.render (f));
}

TEST (bg_the_on_screen_keyboard_plays_through_the_audio_thread)
{
    Instance host;
    if (! CHECK (host.ok()))
        return;
    host.activate();
    CHECK (host.create (1024, 768, 1.0));
    settle (host);
    CHECK (host.rms (4096) < 1.0e-6); // silence
    // a finger on a white key of the strip: note on at the next block, the sound stops when the finger lifts
    host.pointer (SAFIAN_BITMAP_GUI_POINTER_DOWN, 300, 745, 1, SAFIAN_BITMAP_GUI_POINTER_KIND_TOUCH);
    host.gui->tick (host.plugin);
    host.processBlock (256); // the note reaches the engine in this block, the next ones are the sound
    const double sounding = host.rms (8192);
    CHECK (sounding > 1.0e-3);
    host.pointer (SAFIAN_BITMAP_GUI_POINTER_UP, 300, 745, 1, SAFIAN_BITMAP_GUI_POINTER_KIND_TOUCH);
    host.gui->tick (host.plugin);
    host.rms (96000); // the release
    CHECK (host.rms (4096) < sounding * 0.05);
    // the GUI goes away with a key held: its note off is sent, nothing hangs
    host.pointer (SAFIAN_BITMAP_GUI_POINTER_DOWN, 500, 745, 2, SAFIAN_BITMAP_GUI_POINTER_KIND_TOUCH);
    host.processBlock (256);
    CHECK (host.rms (4096) > 1.0e-3);
    host.destroyGui();
    host.processBlock (256);
    host.rms (96000);
    CHECK (host.rms (4096) < 1.0e-4);
}

TEST (bg_the_preset_arrows_step_through_the_presets_on_disk)
{
    const char* userDirEnv = std::getenv ("SYNTHYBOY_USER_DIR");
    const std::filesystem::path presets = std::filesystem::path (userDirEnv != nullptr ? userDirEnv : "/tmp") / "presets";
    std::filesystem::create_directories (presets);
    auto values = synthyboy::ParamValues::defaults();
    values.plain[static_cast<size_t> (synthyboy::findParamIndex (synthyboy::findParamByKey ("osc1.level")->id))] = 77.0f;
    synthyboy::PresetMeta meta = synthyboy::PresetMeta::initDefault();
    meta.name = "Gui Test Preset";
    meta.category = "Test";
    {
        const std::string text = synthyboy::savePreset (meta, values);
        std::ofstream (presets / "gui-test.sbpreset", std::ios::binary) << text;
    }
    Instance host;
    if (! CHECK (host.ok()))
        return;
    host.activate();
    CHECK (host.create (1024, 768, 1.0));
    settle (host);
    FrameInfo f;
    Image before, after;
    host.render (f, &before);
    CHECK_NEAR (host.param ("osc1.level"), 50.0, 1.0e-3);
    host.tap (180, 32); // ">" next preset: the only preset on disk
    host.tickFor (200);
    host.processBlock (64);
    CHECK_NEAR (host.param ("osc1.level"), 77.0, 0.01);
    host.render (f, &after);
    const Rect nameBox { 40, 8, 120, 48 }; // the preset name between the arrows
    CHECK (differingPixels (before, after, &nameBox) > 50);
    CHECK (host.events.stateDirty >= 1);
    CHECK (host.events.rescans >= 1); // the host is told to read the values again
    std::filesystem::remove (presets / "gui-test.sbpreset");
    std::filesystem::remove (presets);
}

TEST (bg_a_macro_knob_of_the_top_row_is_a_parameter_too)
{
    Instance host;
    if (! CHECK (host.ok()))
        return;
    host.activate();
    CHECK (host.create (1024, 768, 1.0));
    settle (host);
    const double before = host.param ("macro.1");
    host.drag (461, 25, 0, -30, 6); // M1
    host.processBlock (64);
    CHECK (host.param ("macro.1") > before + 5.0);
    host.drag (525, 25, 0, -30, 6); // M2
    host.processBlock (64);
    CHECK (host.param ("macro.2") > 5.0);
}

TEST (bg_the_about_line_opens_the_project_page_through_the_host)
{
    Instance host;
    if (! CHECK (host.ok()))
        return;
    host.activate();
    CHECK (host.create (1024, 768, 1.0));
    settle (host);
    host.tap (960, 86); // GLB
    host.tickFor (300);
    // the Preferences panel is further down the page: scroll with the wheel (a mouse wheel over the panels) and touch the grid until the line that links to the project page is hit
    bool opened = false;
    for (int round = 0; round < 40 && ! opened; ++round)
    {
        safian_bitmap_gui_wheel_event_t wheel {};
        wheel.x = 520;
        wheel.y = 400;
        wheel.delta_y = -6.0;
        host.gui->wheel_event (host.plugin, &wheel);
        host.tickFor (30);
        for (int y = 140; y < 650 && ! opened; y += 8)
            for (int x = 40; x < 1000 && ! opened; x += 30)
            {
                host.tap (x, y);
                opened = ! host.events.urls.empty();
            }
    }
    CHECK (opened);
    if (opened)
    {
        std::printf("    open_url: %s\n", host.events.urls.back().c_str());
        CHECK (host.events.urls.back().rfind ("https://", 0) == 0);
    }
}

TEST (bg_the_top_bar_menu_opens_inside_the_frame_and_stays_open)
{
    if (! foregroundForOverlays())
    {
        std::printf ("    SKIPPED: this process cannot become the active application (no window server session): pop-up menus close at once\n");
        return;
    }
    Instance host;
    if (! CHECK (host.ok()))
        return;
    host.activate();
    CHECK (host.create (1024, 768, 1.0));
    host.gui->set_focus (host.plugin, true);
    Image closed = settle (host);
    // the "..." button of the top bar opens the menu; it is a component of the editor, so it is in the frame
    host.tap (407, 32);
    host.tickFor (250);
    FrameInfo f;
    Image open;
    CHECK (host.render (f, &open));
    if (const char* dir = std::getenv ("SYNTHYBOY_BITMAP_GUI_DUMP_DIR"))
        writePng (std::string (dir) + "/menu-open.png", open);
    const Rect below { 380, 90, 200, 300 };
    CHECK (differingPixels (closed, open, &below) > 2000);
    // Taking an entry of the menu (the pop-up polls the pointer and the button with a timer) is NOT verified here: see BITMAPGUI-REPORT.md, open points.
    host.pointer (SAFIAN_BITMAP_GUI_POINTER_LEAVE, 0, 0, 0, SAFIAN_BITMAP_GUI_POINTER_KIND_MOUSE);
    CHECK (host.events.fileRequests.empty());
}

namespace
{
const clap_host_t s_otherHost = { CLAP_VERSION, nullptr, "other-juce-host", "Safian", "", "1.0.0",
                                  [] (const clap_host_t*, const char*) -> const void* { return nullptr; }, [] (const clap_host_t*) {}, [] (const clap_host_t*) {}, [] (const clap_host_t*) {} };
} // namespace

TEST (bg_coexists_with_another_juce_in_the_same_process)
{
    // The JUCE-wrapped CLAP build of the plug-in (SynthyBoy.clap) carries its own copy of JUCE with visible symbols: loaded next to synthyboy.clap it is "another JUCE in the host process"
    // (same version here, the symbols and the toolkit singletons are separate copies all the same). Both run at the same time, on the same thread.
    const char* other = std::getenv ("SYNTHYBOY_JUCE_CLAP_UNDER_TEST");
    if (other == nullptr || other[0] == '\0')
    {
        std::printf ("    SKIPPED: SYNTHYBOY_JUCE_CLAP_UNDER_TEST (the JUCE-wrapped SynthyBoy.clap) is not set: this build has no JUCE plug-in\n");
        return;
    }
    std::string path = other;
    if (std::filesystem::is_directory (path))
        path += "/Contents/MacOS/" + std::filesystem::path (path).stem().string();
    void* lib = dlopen (path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (! CHECK (lib != nullptr))
    {
        std::printf ("    dlopen %s: %s\n", path.c_str(), dlerror());
        return;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*> (dlsym (lib, "clap_entry"));
    if (! CHECK (entry != nullptr && entry->init (other)))
        return;
    const auto* factory = static_cast<const clap_plugin_factory_t*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID));
    const clap_plugin_t* juceInstance = factory != nullptr ? factory->create_plugin (factory, &s_otherHost, "com.safian.synthyboy") : nullptr;
    if (! CHECK (juceInstance != nullptr))
        return;
    CHECK (juceInstance->init (juceInstance));
    CHECK (juceInstance->activate (juceInstance, 48000.0, 32, 256)); // the processor of the other JUCE starts its own message manager, timers and engine

    {
        Instance host;
        if (! CHECK (host.ok()))
            return;
        host.activate();
        CHECK (host.create (1024, 768, 1.0));
        settle (host);
        const double before = host.param ("osc1.level");
        host.drag (587, 498, 0, -40, 6);
        host.processBlock (64);
        CHECK (host.param ("osc1.level") > before);
        host.startAudio();
        host.queueNote (true, 60);
        host.tickFor (400);
        FrameInfo f;
        Image img;
        CHECK (host.render (f, &img));
        host.stopAudio();
        host.destroyGui();
    }
    // the other copy is still alive and well after our toolkit has been used and the GUI destroyed
    CHECK (juceInstance->start_processing (juceInstance));
    juceInstance->stop_processing (juceInstance);
    juceInstance->deactivate (juceInstance);
    juceInstance->destroy (juceInstance);
    entry->deinit();
}

TEST (bg_a_choice_opens_its_list_inside_the_editor)
{
    if (! foregroundForOverlays())
    {
        std::printf ("    SKIPPED: this process cannot become the active application (no window server session): pop-up menus close at once\n");
        return;
    }
    Instance host;
    if (! CHECK (host.ok()))
        return;
    host.activate();
    CHECK (host.create (1024, 768, 1.0));
    host.gui->set_focus (host.plugin, true);
    Image closed = settle (host);
    // the "Curve 1" choice of the oscillator 1 remap: a tap on its name opens the list of entries (a pop-up menu, a child of the editor: it is part of the frame)
    host.tap (924, 487);
    host.tickFor (300);
    FrameInfo f;
    Image open, after;
    CHECK (host.render (f, &open));
    if (const char* dir = std::getenv ("SYNTHYBOY_BITMAP_GUI_DUMP_DIR"))
        writePng (std::string (dir) + "/choice-open.png", open);
    CHECK (differingPixels (closed, open) > 3000);
    // the menu's box is where the frame changed; its entries are 44 points high: touch the middle of the lower half
    int x0 = 99999, y0 = 99999, x1 = 0, y1 = 0;
    for (uint32_t y = 0; y < open.height; ++y)
        for (uint32_t x = 0; x < open.width; ++x)
            if (open.at (x, y) != closed.at (x, y))
            {
                x0 = std::min (x0, static_cast<int> (x)); y0 = std::min (y0, static_cast<int> (y));
                x1 = std::max (x1, static_cast<int> (x)); y1 = std::max (y1, static_cast<int> (y));
            }
    std::printf ("    list box [%d,%d .. %d,%d]\n", x0, y0, x1, y1);
    CHECK (x1 - x0 > 100 && y1 - y0 > 40); // the list: a box of its own, below the choice, inside the frame
    host.tickFor (400);
    CHECK (host.render (f, &after));
    CHECK_EQ (differingPixels (open, after, nullptr), 0u); // and it stays open (no timer of the toolkit closed it because "the application is in the background")
}

TEST (bg_change_messages_and_async_calls_are_delivered_inside_render_without_a_display)
{
    // No display, no dispatch loop of the host: the toolkit's ChangeBroadcaster / AsyncUpdater messages must still arrive, because tick () / render () run the queue.
    // Assigning a modulation by touch (MOD, a source chip, a drag on a knob) ends with ModAssignController::sendChangeMessage (): the knob then shows the ring of the new slot.
    Instance host;
    if (! CHECK (host.ok()))
        return;
    host.activate();
    CHECK (host.create (1024, 768, 1.0));
    Image closed = settle (host);
    host.tap (360, 32); // MOD: the source bar opens
    host.tickFor (200);
    FrameInfo f;
    Image bar;
    CHECK (host.render (f, &bar));
    CHECK (differingPixels (closed, bar) > 5000); // the source bar of the modulation sources (ENV1 .. M2 chips) is in the frame
    host.tap (88, 102); // ENV1: armed
    host.tickFor (100);
    const auto snapshot = [&]
    {
        std::vector<double> all;
        for (uint32_t i = 0; i < host.params->count (host.plugin); ++i)
        {
            clap_param_info_t info {};
            host.params->get_info (host.plugin, i, &info);
            double v = 0.0;
            host.params->get_value (host.plugin, info.id, &v);
            all.push_back (v);
        }
        return all;
    };
    const auto before = snapshot();
    Image beforeUp, afterTick;
    host.pointer (SAFIAN_BITMAP_GUI_POINTER_DOWN, 587, 558, 1, SAFIAN_BITMAP_GUI_POINTER_KIND_MOUSE); // the Level knob of oscillator 1 (the bar moved the panels down by 60)
    for (int i = 1; i <= 8; ++i)
        host.pointer (SAFIAN_BITMAP_GUI_POINTER_MOVE, 587, 558 - 4 * i, 1, SAFIAN_BITMAP_GUI_POINTER_KIND_MOUSE);
    host.processBlock (64);
    CHECK (snapshot() != before); // a modulation slot was written (source, destination, amount)
    host.render (f, &beforeUp);   // the drag is still going on
    host.pointer (SAFIAN_BITMAP_GUI_POINTER_UP, 587, 526, 1, SAFIAN_BITMAP_GUI_POINTER_KIND_MOUSE);
    host.tickFor (250); // the change message of the assignment arrives with the ticks, nothing else happens
    CHECK (host.render (f, &afterTick));
    CHECK (differingPixels (beforeUp, afterTick) > 20); // the knob shows the ring of its new modulation, drawn after the message
}

TEST (bg_every_tab_renders_phone_and_tablet_whatever_the_engine_can_do)
{
    // The same UI on every engine (docs/ui-design/OWN-ENGINE-GAP.md): the own engine of iOS reports almost no capability, so most keys are drawn dimmed. Every top level page must still
    // render, differ from the page before it and stay quiet afterwards (no crash, no asserting component, a settled frame), at the phone and at the tablet size.
    struct Layout { uint32_t w, h; double tabY; double firstX, step; const char* name; };
    const Layout layouts[] = { { 390, 844, 128.0, 24.0, 48.0, "phone" }, { 1024, 768, 86.0, 64.0, 128.0, "tablet" } };
    for (const auto& layout : layouts)
    {
        Instance host;
        if (! CHECK (host.ok()))
            return;
        host.activate();
        CHECK (host.create (layout.w, layout.h, 1.0));
        Image previous = settle (host);
        int changed = 0;
        for (int tab = 1; tab < 8; ++tab) // OSC is the page the GUI starts on: MIX, FLT, ENV, LFO, MOD, FX, GLB
        {
            host.tap (layout.firstX + layout.step * tab, layout.tabY);
            Image now = settle (host, 400);
            CHECK_EQ (now.width, layout.w);
            CHECK_EQ (now.height, layout.h);
            if (now.pixels != previous.pixels)
                ++changed;
            previous = std::move (now);
            FrameInfo f;
            Image again;
            host.render (f, &again);
            CHECK (f.dirty.empty() || (f.flags & SAFIAN_BITMAP_GUI_FRAME_NEEDS_ANOTHER) != 0); // settled, or a live display is moving
        }
        std::printf ("    %s: %d of 7 tab taps changed the picture\n", layout.name, changed);
        CHECK_EQ (changed, 7);
    }
}

namespace
{
/** The touch drag of one finger: `host.drag` ends with a release, the momentum of the scroll runs out while the GUI ticks (`settle`). */
const char* const kScrollWatchedKeys[] = { "osc1.level", "osc1.frame", "osc1.warp.amount", "osc1.morph.amount", "sub.level", "sub.octave", "sub.tune", "sub.pan", "sub.waveform" };

struct WatchedValues
{
    explicit WatchedValues (Instance& host)
    {
        for (const char* key : kScrollWatchedKeys)
            values.push_back (host.param (key));
    }
    bool sameAs (const WatchedValues& other) const { return values == other.values; }
    std::vector<double> values;
};
} // namespace

TEST (bg_one_finger_drag_on_a_card_background_scrolls_the_panels)
{
    Instance host;
    if (! CHECK (host.ok()))
        return;
    host.activate();
    CHECK (host.create (1024, 768, 1.0));
    Image before = settle (host);
    const WatchedValues valuesBefore (host);
    const Rect panels { 0, 112, 1024, 548 }; // the panel area between the tabs and the piano strip

    // empty space of the Oscillator 1 card (right of its "More" button): nothing there uses the drag; one finger up by 240 points scrolls the panels down
    // (the toolkit's drag scrolling keeps a momentum after the release: the content ends at least where the finger stopped, how much further depends on the speed of the moves)
    const uint32_t touch = SAFIAN_BITMAP_GUI_POINTER_KIND_TOUCH;
    host.pointer (SAFIAN_BITMAP_GUI_POINTER_DOWN, 530, 582, 1, touch);
    for (int i = 1; i <= 12; ++i)
    {
        host.pointer (SAFIAN_BITMAP_GUI_POINTER_MOVE, 530, 582 - 20 * i, 1, touch);
        host.tickFor (16);
    }
    host.pointer (SAFIAN_BITMAP_GUI_POINTER_UP, 530, 342, 1, touch);
    Image after = settle (host, 900);
    host.processBlock (64);
    CHECK (differingPixels (before, after, &panels) > 60000); // most of the panel area is other content now
    CHECK (valuesBefore.sameAs (WatchedValues (host)));       // and no parameter was touched on the way

    // the content moved as a whole, by about the finger's travel (plus the momentum of the release): some row offset of the first column of panels (the Sub card, static) maps the old picture onto the new one
    int bestShift = 0;
    uint64_t bestDiff = ~0ull;
    for (int shift = 100; shift <= 520; ++shift)
    {
        uint64_t diff = 0;
        const int height = 650 - shift - 124;
        for (int y = 124; y < 124 + height; ++y)
            for (int x = 16; x < 330; ++x)
                diff += before.at (static_cast<uint32_t> (x), static_cast<uint32_t> (y + shift)) != after.at (static_cast<uint32_t> (x), static_cast<uint32_t> (y));
        if (diff < bestDiff)
        {
            bestDiff = diff;
            bestShift = shift;
        }
    }
    std::printf ("    the panels moved up by %d pixels (finger 240), %llu pixels differ after the shift\n", bestShift, static_cast<unsigned long long> (bestDiff));
    CHECK (bestShift >= 236); // the content followed the finger (240, not 240 less the 8 point threshold)
    CHECK_EQ (bestDiff, 0u);
}

TEST (bg_one_finger_drag_on_a_knob_changes_the_parameter_and_does_not_scroll)
{
    Instance host;
    if (! CHECK (host.ok()))
        return;
    host.activate();
    CHECK (host.create (1024, 768, 1.0));
    Image before = settle (host);
    // two columns of panels that the Level knob of Oscillator 1 is in neither of (the Sub panel at the left, the Oscillator 1 Remap panel at the right): they stay where they are
    const Rect leftColumn { 8, 118, 330, 540 };
    const Rect rightColumn { 684, 118, 316, 540 };
    const double level = host.param ("osc1.level");

    host.drag (587, 498, 0, -70, 10);
    host.processBlock (64);
    Image after = settle (host, 900);
    CHECK (host.param ("osc1.level") > level + 0.05);
    CHECK_EQ (differingPixels (before, after, &leftColumn), 0u);
    CHECK_EQ (differingPixels (before, after, &rightColumn), 0u);

    // a drag that starts on the knob and ends on a background (far to the side and up) is still the knob's: no scroll
    const double level2 = host.param ("osc1.level");
    host.drag (587, 498, -70, 60, 10); // ends on the Oscillator 1 Morph Amount knob and below it: a background
    host.processBlock (64);
    Image ended = settle (host, 900);
    CHECK (host.param ("osc1.level") < level2 - 0.01);
    CHECK_EQ (differingPixels (before, ended, &leftColumn), 0u);
    CHECK_EQ (differingPixels (before, ended, &rightColumn), 0u);
}

TEST (bg_a_tap_acts_a_drag_from_a_button_does_not_scroll_and_a_drag_from_a_toggle_scrolls_without_a_click)
{
    Instance host;
    if (! CHECK (host.ok()))
        return;
    host.activate();
    CHECK (host.create (1024, 768, 1.0));
    Image before = settle (host);
    const Rect panels { 0, 112, 1024, 548 };
    const double wave = host.param ("sub.waveform");

    // a drag that starts on the SQR button of the Sub panel and ends on the card: the panels stay, the button does not click
    host.drag (203, 269, 0, -150, 10);
    host.processBlock (64);
    Image dragged = settle (host, 900);
    CHECK_NEAR (host.param ("sub.waveform"), wave, 1.0e-9);
    CHECK_EQ (differingPixels (before, dragged, &panels), 0u);

    // a short touch is a tap: SQR acts
    host.tap (203, 269);
    host.processBlock (64);
    CHECK (std::fabs (host.param ("sub.waveform") - wave) > 1.0e-6);

    // a control that is only tapped (the "Osc 1 Remap On" toggle) toggles for a tap ...
    const double remapOn = host.param ("osc1.remap.on");
    host.tap (740, 480);
    host.processBlock (64);
    CHECK (std::fabs (host.param ("osc1.remap.on") - remapOn) > 1.0e-6);
    host.tap (740, 480);
    host.processBlock (64);
    CHECK_NEAR (host.param ("osc1.remap.on"), remapOn, 1.0e-9);
    // ... and does not use the drag: a drag that starts on it scrolls the panels, and the release is no tap
    host.drag (740, 480, 0, -60, 10);
    host.processBlock (64);
    Image scrolled = settle (host, 900);
    CHECK_NEAR (host.param ("osc1.remap.on"), remapOn, 1.0e-9);
    CHECK (differingPixels (dragged, scrolled, &panels) > 20000);
}

TEST (bg_zz_module_shutdown)
{
    Module::get().shutdown(); // clap_entry.deinit (): the toolkit of the module goes with it
    CHECK (true);
}
