// SynthyBoy - tests of the bitmap GUI: a CLAP host without any JUCE (dlopens the built .clap, drives com.safian.bitmap-gui/1, runs the audio thread).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// What a host such as Kick does with the extension, written as plainly as possible: no toolkit, no windowing, pixels in memory. The module under test is named by the environment variable
// SYNTHYBOY_CLAP_UNDER_TEST (ctest sets it to the built synthyboy.clap).
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <dlfcn.h>
#include <filesystem>

#include <clap/clap.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include "bitmap_gui_ext.h"
#include "synthyboy/params.h"

#if defined (__APPLE__)
bool bgtestMakeProcessForeground(); // foreground_mac.mm
#endif

namespace bgtest
{
using Clock = std::chrono::steady_clock;

inline void sleepMs (int ms) { std::this_thread::sleep_for (std::chrono::milliseconds (ms)); }

/** SYNTHYBOY_TEST_TIME_SCALE (default 1): the waits and windows of the tests are multiplied by it, for emulated CPUs (the ARMv7 module under qemu runs 20 to 50 times slower than the real thing). */
inline double timeScale()
{
    static const double scale = [] { const char* v = std::getenv ("SYNTHYBOY_TEST_TIME_SCALE"); return v != nullptr ? std::max (1.0, std::atof (v)) : 1.0; }();
    return scale;
}
inline int scaled (int ms) { return static_cast<int> (ms * timeScale()); }

/** True when pop-up menus and call-outs of the GUI can stay open in this process: Linux (the module marks the process active itself, there is no X) and Windows (a foreground window)
    always; macOS needs the active application, which the test host makes itself (false: no window server session, the tests that open a menu or a sheet then say so and are skipped). */
inline bool foregroundForOverlays()
{
#if defined (__APPLE__)
    static const bool foreground = ::bgtestMakeProcessForeground();
    return foreground;
#else
    return true;
#endif
}

/** The module: loaded once per process, clap_entry.init () / deinit () around the tests. */
class Module
{
public:
    static Module& get()
    {
        static Module m;
        return m;
    }
    const clap_plugin_factory_t* factory = nullptr;
    const clap_plugin_entry_t* entry = nullptr;
    std::string path;

    void shutdown()
    {
        if (entry != nullptr)
            entry->deinit();
        entry = nullptr;
        factory = nullptr;
    }

private:
    Module()
    {
        // a clean user directory for every run of the test program (presets, preferences, the content library: content is stored once by its hash, a second run would find the first one's files)
        if (const char* userDir = std::getenv ("SYNTHYBOY_USER_DIR"); userDir != nullptr && userDir[0] != '\0')
        {
            std::error_code ec;
            std::filesystem::remove_all (userDir, ec);
            std::filesystem::create_directories (userDir, ec);
        }
        const char* env = std::getenv ("SYNTHYBOY_CLAP_UNDER_TEST");
        if (env == nullptr || env[0] == '\0')
        {
            std::printf ("    SYNTHYBOY_CLAP_UNDER_TEST is not set\n");
            return;
        }
        path = env;
        std::string file = path;
        void* lib = dlopen (file.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (lib == nullptr)
        {
            std::printf ("    dlopen %s: %s\n", file.c_str(), dlerror());
            return;
        }
        entry = static_cast<const clap_plugin_entry_t*> (dlsym (lib, "clap_entry"));
        if (entry == nullptr || ! entry->init (file.c_str()))
        {
            entry = nullptr;
            return;
        }
        factory = static_cast<const clap_plugin_factory_t*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID));
    }
};

struct Rect
{
    int32_t x = 0, y = 0;
    uint32_t w = 0, h = 0;
    uint64_t area() const { return static_cast<uint64_t> (w) * h; }
};

/** A copy of a frame (the pointer of the plug-in dies with the next render). */
struct Image
{
    uint32_t width = 0, height = 0;
    std::vector<uint32_t> pixels; // 0xAARRGGBB, tightly packed

    uint32_t at (uint32_t x, uint32_t y) const { return pixels[static_cast<size_t> (y) * width + x]; }
};

struct FrameInfo
{
    uint64_t id = 0;
    uint32_t flags = 0, nextFrameMs = 0, format = 0, stride = 0;
    std::vector<Rect> dirty;
    uint64_t dirtyArea() const
    {
        uint64_t a = 0;
        for (const auto& r : dirty)
            a += r.area();
        return a;
    }
};

struct HostEvents
{
    std::mutex lock;
    int renderRequests = 0, tickRequests = 0, cursorChanges = 0, flushRequests = 0, rescans = 0, stateDirty = 0, restartRequests = 0;
    uint32_t lastCursor = 0;
    struct TextInput { bool show; Rect field; uint32_t kind; std::string text; };
    std::vector<TextInput> textInput;
    struct FileRequest { uint32_t id; safian_bitmap_gui_file_request_t request; std::string title, wildcards, suggested, purpose; };
    std::vector<FileRequest> fileRequests;
    std::vector<std::pair<uint32_t, std::string>> shares;
    std::vector<std::string> urls;
    std::vector<clap_id> remotePages;
    std::vector<std::pair<uint32_t, uint32_t>> resizeRequests;
    // output events of process () / flush (): parameter values and gestures
    struct ParamOut { uint16_t type; clap_id id; double value; };
    std::vector<ParamOut> paramOut;
};

/** One plug-in instance with a host around it. */
class Instance
{
public:
    explicit Instance (bool offerBitmapHost = true, bool offerFiles = true) : offerBitmap (offerBitmapHost)
    {
        host.clap_version = CLAP_VERSION;
        host.host_data = this;
        host.name = "bitmap-gui-test-host";
        host.vendor = "Safian";
        host.url = "";
        host.version = "1.0.0";
        host.get_extension = [] (const clap_host_t* h, const char* id) -> const void* { return static_cast<Instance*> (h->host_data)->hostExtension (id); };
        host.request_restart = [] (const clap_host_t* h) { ++static_cast<Instance*> (h->host_data)->events.restartRequests; };
        host.request_process = [] (const clap_host_t*) {};
        host.request_callback = [] (const clap_host_t*) {};
        hostParams.rescan = [] (const clap_host_t* h, clap_param_rescan_flags) { ++static_cast<Instance*> (h->host_data)->events.rescans; };
        hostParams.clear = [] (const clap_host_t*, clap_id, clap_param_clear_flags) {};
        hostParams.request_flush = [] (const clap_host_t* h) { ++static_cast<Instance*> (h->host_data)->events.flushRequests; };
        hostState.mark_dirty = [] (const clap_host_t* h) { ++static_cast<Instance*> (h->host_data)->events.stateDirty; };

        hostGui.request_render = [] (const clap_host_t* h, const safian_bitmap_gui_rect_t*) { ++static_cast<Instance*> (h->host_data)->events.renderRequests; };
        hostGui.request_tick = [] (const clap_host_t* h, uint32_t) { ++static_cast<Instance*> (h->host_data)->events.tickRequests; };
        hostGui.request_resize = [] (const clap_host_t* h, uint32_t w, uint32_t hgt) -> bool
        {
            static_cast<Instance*> (h->host_data)->events.resizeRequests.push_back ({ w, hgt });
            return true;
        };
        hostGui.set_cursor = [] (const clap_host_t* h, uint32_t cursor)
        {
            auto* self = static_cast<Instance*> (h->host_data);
            ++self->events.cursorChanges;
            self->events.lastCursor = cursor;
        };
        hostGui.request_text_input = [] (const clap_host_t* h, bool show, const safian_bitmap_gui_rect_t* field, uint32_t kind, const char* text)
        {
            HostEvents::TextInput t { show, {}, kind, text != nullptr ? text : "" };
            if (field != nullptr)
                t.field = { field->x, field->y, field->width, field->height };
            static_cast<Instance*> (h->host_data)->events.textInput.push_back (t);
        };
        if (offerFiles)
        {
            hostGui.request_file = [] (const clap_host_t* h, uint32_t id, const safian_bitmap_gui_file_request_t* r) -> bool
            {
                HostEvents::FileRequest f { id, *r, r->title, r->wildcards, r->suggested_name, r->purpose };
                static_cast<Instance*> (h->host_data)->events.fileRequests.push_back (f);
                return true;
            };
            hostGui.share_file = [] (const clap_host_t* h, uint32_t id, const char* path, const char*) -> bool
            {
                static_cast<Instance*> (h->host_data)->events.shares.push_back ({ id, path });
                return true;
            };
        }
        hostGui.open_url = [] (const clap_host_t* h, const char* url) -> bool
        {
            static_cast<Instance*> (h->host_data)->events.urls.push_back (url);
            return true;
        };
        hostGui.set_active_remote_page = [] (const clap_host_t* h, clap_id page) { static_cast<Instance*> (h->host_data)->events.remotePages.push_back (page); };

        auto& module = Module::get();
        if (module.factory != nullptr)
            plugin = module.factory->create_plugin (module.factory, &host, "com.safian.synthyboy");
        if (plugin != nullptr)
        {
            plugin->init (plugin);
            gui = static_cast<const safian_plugin_bitmap_gui_t*> (plugin->get_extension (plugin, SAFIAN_EXT_BITMAP_GUI));
            params = static_cast<const clap_plugin_params_t*> (plugin->get_extension (plugin, CLAP_EXT_PARAMS));
        }
    }

    ~Instance()
    {
        stopAudio();
        if (plugin != nullptr)
        {
            if (processing)
                plugin->stop_processing (plugin);
            if (active)
                plugin->deactivate (plugin);
            plugin->destroy (plugin);
        }
    }

    Instance (const Instance&) = delete;
    Instance& operator= (const Instance&) = delete;

    bool ok() const { return plugin != nullptr && gui != nullptr && params != nullptr; }

    // ---- GUI ----
    bool create (uint32_t w, uint32_t h, double scale = 1.0, uint32_t flags = 0) { return gui->create (plugin, w, h, scale, flags); }
    void destroyGui() { gui->destroy (plugin); }

    /** Ticks and renders once; copies the frame when `image` is given. */
    bool render (FrameInfo& info, Image* image = nullptr)
    {
        safian_bitmap_gui_frame_t f {};
        if (! gui->render (plugin, &f))
            return false;
        info.id = f.frame_id;
        info.flags = f.flags;
        info.nextFrameMs = f.next_frame_ms;
        info.format = f.pixel_format;
        info.stride = f.stride_bytes;
        info.dirty.clear();
        for (uint32_t i = 0; i < f.dirty_count; ++i)
            info.dirty.push_back ({ f.dirty[i].x, f.dirty[i].y, f.dirty[i].width, f.dirty[i].height });
        if (image != nullptr)
        {
            image->width = f.width;
            image->height = f.height;
            image->pixels.resize (static_cast<size_t> (f.width) * f.height);
            for (uint32_t y = 0; y < f.height; ++y)
                std::memcpy (&image->pixels[static_cast<size_t> (y) * f.width], f.pixels + static_cast<size_t> (y) * f.stride_bytes, static_cast<size_t> (f.width) * 4);
        }
        return true;
    }

    void pointer (uint32_t type, double x, double y, uint32_t id = 0, uint32_t kind = SAFIAN_BITMAP_GUI_POINTER_KIND_MOUSE, uint32_t modifiers = 0)
    {
        safian_bitmap_gui_pointer_event_t e {};
        e.type = type;
        e.kind = kind;
        e.pointer_id = id;
        e.x = x;
        e.y = y;
        e.buttons = (type == SAFIAN_BITMAP_GUI_POINTER_DOWN || type == SAFIAN_BITMAP_GUI_POINTER_MOVE) ? SAFIAN_BITMAP_GUI_BUTTON_PRIMARY : 0;
        e.modifiers = modifiers;
        e.pressure = 0.0f;
        gui->pointer_event (plugin, &e);
    }
    void tap (double x, double y, uint32_t id = 0, uint32_t kind = SAFIAN_BITMAP_GUI_POINTER_KIND_TOUCH)
    {
        pointer (SAFIAN_BITMAP_GUI_POINTER_DOWN, x, y, id, kind);
        pointer (SAFIAN_BITMAP_GUI_POINTER_UP, x, y, id, kind);
    }
    /** A mouse press as a user makes it: the pointer comes, rests, the button is down for a while, goes up. Pop-up menus take their entry from the pointer position and the button state they poll
        with a timer: a click that is over within one tick is invisible to them. */
    void press (double x, double y, int restMs = 200, int holdMs = 300)
    {
        pointer (SAFIAN_BITMAP_GUI_POINTER_HOVER, x, y, 0, SAFIAN_BITMAP_GUI_POINTER_KIND_MOUSE);
        tickFor (restMs);
        pointer (SAFIAN_BITMAP_GUI_POINTER_DOWN, x, y, 0, SAFIAN_BITMAP_GUI_POINTER_KIND_MOUSE);
        tickFor (holdMs);
        pointer (SAFIAN_BITMAP_GUI_POINTER_UP, x, y, 0, SAFIAN_BITMAP_GUI_POINTER_KIND_MOUSE);
        tickFor (restMs);
    }
    /** A drag of the touch pointer from (x, y) by (dx, dy) in steps, ticking between the steps. */
    void drag (double x, double y, double dx, double dy, int steps = 8, uint32_t id = 0)
    {
        pointer (SAFIAN_BITMAP_GUI_POINTER_DOWN, x, y, id, SAFIAN_BITMAP_GUI_POINTER_KIND_TOUCH);
        for (int i = 1; i <= steps; ++i)
        {
            pointer (SAFIAN_BITMAP_GUI_POINTER_MOVE, x + dx * i / steps, y + dy * i / steps, id, SAFIAN_BITMAP_GUI_POINTER_KIND_TOUCH);
            gui->tick (plugin);
        }
        pointer (SAFIAN_BITMAP_GUI_POINTER_UP, x + dx, y + dy, id, SAFIAN_BITMAP_GUI_POINTER_KIND_TOUCH);
        gui->tick (plugin);
    }
    void key (uint32_t code, const char* text = nullptr, uint32_t modifiers = 0)
    {
        safian_bitmap_gui_key_event_t e {};
        e.type = SAFIAN_BITMAP_GUI_KEY_DOWN_EVENT;
        e.key_code = code;
        e.modifiers = modifiers;
        e.text = text;
        gui->key_event (plugin, &e);
        e.type = SAFIAN_BITMAP_GUI_KEY_UP_EVENT;
        e.text = nullptr;
        gui->key_event (plugin, &e);
    }
    void tickFor (int milliseconds, int stepMs = 8)
    {
        const auto end = Clock::now() + std::chrono::milliseconds (scaled (milliseconds));
        while (Clock::now() < end)
        {
            gui->tick (plugin);
            sleepMs (stepMs);
        }
    }

    // ---- audio ----
    void activate (double sampleRate = 48000.0, uint32_t maxFrames = 256)
    {
        rate = sampleRate;
        block = maxFrames;
        plugin->activate (plugin, sampleRate, 32, maxFrames);
        active = true;
        plugin->start_processing (plugin);
        processing = true;
    }

    /** Parameter value as the host reads it. */
    double param (const char* key) const
    {
        double v = 0.0;
        params->get_value (plugin, synthyboy::findParamByKey (key)->id, &v);
        return v;
    }
    void queueParam (const char* key, double value)
    {
        std::lock_guard<std::mutex> g (inLock);
        clap_event_param_value_t e {};
        e.header.size = sizeof e;
        e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        e.header.type = CLAP_EVENT_PARAM_VALUE;
        e.param_id = synthyboy::findParamByKey (key)->id;
        e.note_id = -1;
        e.port_index = -1;
        e.channel = -1;
        e.key = -1;
        e.value = value;
        paramQueue.push_back (e);
    }
    void queueNote (bool on, int key, double velocity = 0.8)
    {
        std::lock_guard<std::mutex> g (inLock);
        clap_event_note_t e {};
        e.header.size = sizeof e;
        e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        e.header.type = on ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF;
        e.note_id = -1;
        e.port_index = 0;
        e.channel = 0;
        e.key = static_cast<int16_t> (key);
        e.velocity = velocity;
        noteQueue.push_back (e);
    }

    /** One block on the calling thread; returns the left channel. */
    std::vector<float> processBlock (uint32_t frames)
    {
        std::vector<clap_event_param_value_t> params;
        std::vector<clap_event_note_t> notes;
        {
            std::lock_guard<std::mutex> g (inLock);
            params.swap (paramQueue);
            notes.swap (noteQueue);
        }
        struct List
        {
            std::vector<const clap_event_header_t*> items;
        } list;
        for (auto& p : params)
            list.items.push_back (&p.header);
        for (auto& n : notes)
            list.items.push_back (&n.header);
        clap_input_events_t in {};
        in.ctx = &list;
        in.size = [] (const clap_input_events_t* l) -> uint32_t { return static_cast<uint32_t> (static_cast<List*> (l->ctx)->items.size()); };
        in.get = [] (const clap_input_events_t* l, uint32_t i) -> const clap_event_header_t* { return static_cast<List*> (l->ctx)->items[i]; };
        clap_output_events_t out {};
        out.ctx = this;
        out.try_push = [] (const clap_output_events_t* o, const clap_event_header_t* h) -> bool
        {
            auto* self = static_cast<Instance*> (o->ctx);
            std::lock_guard<std::mutex> g (self->events.lock);
            if (h->type == CLAP_EVENT_PARAM_VALUE)
                self->events.paramOut.push_back ({ h->type, reinterpret_cast<const clap_event_param_value_t*> (h)->param_id, reinterpret_cast<const clap_event_param_value_t*> (h)->value });
            else if (h->type == CLAP_EVENT_PARAM_GESTURE_BEGIN || h->type == CLAP_EVENT_PARAM_GESTURE_END)
                self->events.paramOut.push_back ({ h->type, reinterpret_cast<const clap_event_param_gesture_t*> (h)->param_id, 0.0 });
            return true;
        };
        std::vector<float> left (frames, 0.0f), right (frames, 0.0f);
        float* channels[2] = { left.data(), right.data() };
        clap_audio_buffer_t buffer {};
        buffer.data32 = channels;
        buffer.channel_count = 2;
        clap_process_t process {};
        process.steady_time = -1;
        process.frames_count = frames;
        process.audio_outputs = &buffer;
        process.audio_outputs_count = 1;
        process.in_events = &in;
        process.out_events = &out;
        plugin->process (plugin, &process);
        return left;
    }

    /** The audio thread: runs blocks at about real time until stopAudio (). */
    void startAudio()
    {
        if (audioThread.joinable())
            return;
        audioRun = true;
        audioThread = std::thread ([this]
        {
            while (audioRun.load())
            {
                const auto left = processBlock (block);
                double sum = 0.0;
                for (float s : left)
                    sum += static_cast<double> (s) * s;
                energy.store (std::sqrt (sum / left.size()));
                ++blocks;
                sleepMs (4);
            }
        });
    }
    void stopAudio()
    {
        audioRun = false;
        if (audioThread.joinable())
            audioThread.join();
    }

    double rms (uint32_t frames = 4096)
    {
        double sum = 0.0;
        uint32_t done = 0;
        while (done < frames)
        {
            const auto left = processBlock (block);
            for (float s : left)
                sum += static_cast<double> (s) * s;
            done += block;
        }
        return std::sqrt (sum / done);
    }

    clap_host_t host {};
    clap_host_params_t hostParams {};
    clap_host_state_t hostState {};
    safian_host_bitmap_gui_t hostGui {};
    HostEvents events;
    const clap_plugin_t* plugin = nullptr;
    const safian_plugin_bitmap_gui_t* gui = nullptr;
    const clap_plugin_params_t* params = nullptr;
    std::atomic<double> energy { 0.0 };
    std::atomic<int> blocks { 0 };

private:
    const void* hostExtension (const char* id)
    {
        if (std::strcmp (id, CLAP_EXT_PARAMS) == 0)
            return &hostParams;
        if (std::strcmp (id, CLAP_EXT_STATE) == 0)
            return &hostState;
        if (offerBitmap && std::strcmp (id, SAFIAN_EXT_BITMAP_GUI_HOST) == 0)
            return &hostGui;
        return nullptr;
    }

    bool offerBitmap;
    bool active = false, processing = false;
    double rate = 48000.0;
    uint32_t block = 256;
    std::mutex inLock;
    std::vector<clap_event_param_value_t> paramQueue;
    std::vector<clap_event_note_t> noteQueue;
    std::thread audioThread;
    std::atomic<bool> audioRun { false };
};

/** The pixels of `b` that differ from `a` (any channel), in the rectangle r (clipped). */
inline uint64_t differingPixels (const Image& a, const Image& b, const Rect* r = nullptr)
{
    if (a.width != b.width || a.height != b.height)
        return static_cast<uint64_t> (a.width) * a.height;
    uint32_t x0 = 0, y0 = 0, x1 = a.width, y1 = a.height;
    if (r != nullptr)
    {
        x0 = static_cast<uint32_t> (std::max (0, r->x));
        y0 = static_cast<uint32_t> (std::max (0, r->y));
        x1 = std::min (a.width, static_cast<uint32_t> (std::max (0, r->x)) + r->w);
        y1 = std::min (a.height, static_cast<uint32_t> (std::max (0, r->y)) + r->h);
    }
    uint64_t n = 0;
    for (uint32_t y = y0; y < y1; ++y)
        for (uint32_t x = x0; x < x1; ++x)
            n += a.at (x, y) != b.at (x, y);
    return n;
}
} // namespace bgtest
