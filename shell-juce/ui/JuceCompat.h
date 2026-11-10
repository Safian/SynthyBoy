// SynthyBoy - UI: the few places where JUCE 7.0.12 (the Kick app) and JUCE 8.x (the plug-in) differ.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The library compiles against both. Everything else it uses is in the common subset; new code that touches fonts, text measuring or another
// API that moved between 7 and 8 goes through this header. Checked by compiling the library against the JUCE 7.0.12 headers (UILIB-REPORT.md).
#pragma once

#include <juce_graphics/juce_graphics.h>

namespace synthyboy::ui::compat
{
/** A font of `height` (pixels, JUCE's font height), plain or bold, of the default typeface. */
inline juce::Font makeFont (float height, bool bold)
{
#if JUCE_MAJOR_VERSION >= 8
    auto options = juce::FontOptions().withHeight (height);
    if (bold)
        options = options.withStyle ("Bold");
    return juce::Font (options);
#else
    return juce::Font (height, bold ? juce::Font::bold : juce::Font::plain);
#endif
}

/** The width of `text` in `font`, in pixels. */
inline float stringWidth (const juce::Font& font, const juce::String& text)
{
#if JUCE_MAJOR_VERSION >= 8
    return juce::GlyphArrangement::getStringWidth (font, text);
#else
    return font.getStringWidthFloat (text);
#endif
}
} // namespace synthyboy::ui::compat
