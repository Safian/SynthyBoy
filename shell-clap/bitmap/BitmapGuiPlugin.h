// SynthyBoy - the plug-in side of the CLAP extension com.safian.bitmap-gui/1 (shell-clap/bitmap_gui_ext.h, docs/BITMAP-GUI-EXTENSION.md).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The only header of this folder that the JUCE free part of the shell (synthyboy_clap.cpp) includes.
#pragma once

#include "bitmap_gui_ext.h"

namespace synthyboy
{
/** The vtable the plug-in returns for SAFIAN_EXT_BITMAP_GUI. */
const safian_plugin_bitmap_gui_t* bitmapGuiExtension() noexcept;

/** The module is unloaded (clap_plugin_entry::deinit): the toolkit that the GUIs of this module share is shut down when no GUI is left. */
void bitmapGuiShutdown() noexcept;
} // namespace synthyboy
