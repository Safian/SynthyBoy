// SynthyBoy - the plug-in side of the CLAP extension com.safian.bitmap-gui/1 (see BitmapGuiPlugin.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "BitmapGuiPlugin.h"

#include <memory>

#include "BitmapSession.h"
#include "JuceRuntime.h"
#include "synthyboy_plugin.h"

namespace synthyboy
{
namespace
{
using bitmap::BitmapSession;

BitmapSession* sessionOf (const clap_plugin_t* plugin) noexcept
{
    if (plugin == nullptr || plugin->plugin_data == nullptr)
        return nullptr;
    return static_cast<BitmapSession*> (toClap (plugin)->bitmapGui.get());
}

// Nothing may leave these functions as an exception: they are called from C of the host.
template <typename F>
bool guarded (F&& f) noexcept
{
    try
    {
        return f();
    }
    catch (...)
    {
        return false;
    }
}

bool ext_create (const clap_plugin_t* plugin, uint32_t width, uint32_t height, double scale, uint32_t flags) noexcept
{
    if (plugin == nullptr || plugin->plugin_data == nullptr)
        return false;
    auto* self = toClap (plugin);
    if (self->bitmapGui != nullptr)
        return false;
    return guarded ([&]
    {
        self->bitmapGui = std::make_unique<BitmapSession> (*self, width, height, scale, flags);
        return true;
    });
}

void ext_destroy (const clap_plugin_t* plugin) noexcept
{
    if (plugin == nullptr || plugin->plugin_data == nullptr)
        return;
    guarded ([&]
    {
        toClap (plugin)->bitmapGui.reset();
        return true;
    });
}

bool ext_get_info (const clap_plugin_t*, safian_bitmap_gui_info_t* info) noexcept
{
    if (info == nullptr)
        return false;
    info->flags = SAFIAN_BITMAP_GUI_CAP_MULTITOUCH | SAFIAN_BITMAP_GUI_CAP_KEYBOARD | SAFIAN_BITMAP_GUI_CAP_TEXT_INPUT | SAFIAN_BITMAP_GUI_CAP_FILE_REQUESTS;
    info->pixel_format = SAFIAN_BITMAP_GUI_PIXEL_BGRA8888_PREMUL;
    info->max_dirty_rects = BitmapSession::kMaxDirtyRects;
    info->min_scale = BitmapSession::kMinScale;
    info->max_scale = BitmapSession::kMaxScale;
    return true;
}

bool ext_get_preferred_size (const clap_plugin_t*, uint32_t* width, uint32_t* height) noexcept
{
    if (width == nullptr || height == nullptr)
        return false;
    *width = BitmapSession::kDefaultWidth;
    *height = BitmapSession::kDefaultHeight;
    return true;
}

bool ext_can_resize (const clap_plugin_t*) noexcept { return true; }

bool ext_get_size_limits (const clap_plugin_t*, safian_bitmap_gui_size_limits_t* limits) noexcept
{
    if (limits == nullptr)
        return false;
    limits->min_width = 360; // ui::SynthyBoyEditor::kMinWidth .. kMaxHeight (static_asserted in BitmapGuiPlugin.cpp of the build with the editor)
    limits->min_height = 480;
    limits->max_width = 1800;
    limits->max_height = 1400;
    limits->aspect_numerator = 0;
    limits->aspect_denominator = 0;
    return true;
}

bool ext_get_size (const clap_plugin_t* plugin, uint32_t* width, uint32_t* height) noexcept
{
    auto* session = sessionOf (plugin);
    if (session == nullptr || width == nullptr || height == nullptr)
        return false;
    session->getSize (*width, *height);
    return true;
}

bool ext_set_size (const clap_plugin_t* plugin, uint32_t width, uint32_t height) noexcept
{
    auto* session = sessionOf (plugin);
    return session != nullptr && guarded ([&] { return session->setSize (width, height); });
}

bool ext_set_scale (const clap_plugin_t* plugin, double scale) noexcept
{
    auto* session = sessionOf (plugin);
    return session != nullptr && guarded ([&] { return session->setScale (scale); });
}

bool ext_tick (const clap_plugin_t* plugin) noexcept
{
    auto* session = sessionOf (plugin);
    return session != nullptr && guarded ([&] { return session->tick(); });
}

bool ext_is_dirty (const clap_plugin_t* plugin) noexcept
{
    auto* session = sessionOf (plugin);
    return session != nullptr && session->isDirty();
}

bool ext_render (const clap_plugin_t* plugin, safian_bitmap_gui_frame_t* frame) noexcept
{
    auto* session = sessionOf (plugin);
    return session != nullptr && frame != nullptr && guarded ([&] { return session->render (*frame); });
}

bool ext_pointer_event (const clap_plugin_t* plugin, const safian_bitmap_gui_pointer_event_t* event) noexcept
{
    auto* session = sessionOf (plugin);
    return session != nullptr && event != nullptr && guarded ([&] { return session->pointerEvent (*event); });
}

bool ext_wheel_event (const clap_plugin_t* plugin, const safian_bitmap_gui_wheel_event_t* event) noexcept
{
    auto* session = sessionOf (plugin);
    return session != nullptr && event != nullptr && guarded ([&] { return session->wheelEvent (*event); });
}

bool ext_key_event (const clap_plugin_t* plugin, const safian_bitmap_gui_key_event_t* event) noexcept
{
    auto* session = sessionOf (plugin);
    return session != nullptr && event != nullptr && guarded ([&] { return session->keyEvent (*event); });
}

bool ext_text_input (const clap_plugin_t* plugin, const char* utf8) noexcept
{
    auto* session = sessionOf (plugin);
    return session != nullptr && utf8 != nullptr && guarded ([&] { return session->textInput (utf8); });
}

void ext_set_focus (const clap_plugin_t* plugin, bool focused) noexcept
{
    if (auto* session = sessionOf (plugin))
        guarded ([&] { session->setFocus (focused); return true; });
}

void ext_request_done (const clap_plugin_t* plugin, uint32_t requestId, uint32_t result, const char* path) noexcept
{
    if (auto* session = sessionOf (plugin))
        guarded ([&] { session->requestDone (requestId, result, path); return true; });
}

bool ext_show_remote_page (const clap_plugin_t* plugin, clap_id pageId) noexcept
{
    auto* session = sessionOf (plugin);
    return session != nullptr && guarded ([&] { return session->showRemotePage (pageId); });
}

const safian_plugin_bitmap_gui_t s_extension = {
    ext_create,
    ext_destroy,
    ext_get_info,
    ext_get_preferred_size,
    ext_can_resize,
    ext_get_size_limits,
    ext_get_size,
    ext_set_size,
    ext_set_scale,
    ext_tick,
    ext_is_dirty,
    ext_render,
    ext_pointer_event,
    ext_wheel_event,
    ext_key_event,
    ext_text_input,
    ext_set_focus,
    ext_request_done,
    ext_show_remote_page
};

static_assert (ui::SynthyBoyEditor::kMinWidth == 360 && ui::SynthyBoyEditor::kMinHeight == 480 && ui::SynthyBoyEditor::kMaxWidth == 1800 && ui::SynthyBoyEditor::kMaxHeight == 1400,
               "the size limits of the extension are the editor's");
} // namespace

const safian_plugin_bitmap_gui_t* bitmapGuiExtension() noexcept
{
    return &s_extension;
}

void bitmapGuiShutdown() noexcept
{
    try
    {
        bitmap::JuceRuntime::shutdown();
    }
    catch (...)
    {
    }
}
} // namespace synthyboy
