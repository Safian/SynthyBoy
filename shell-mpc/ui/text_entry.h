// SynthyBoy - MPC UI component library: TEXT ENTRY, an on-screen keyboard page of our own (the host offers no text entry to a plugin-drawn page): a text field with a caret, a QWERTY layout with a
// shift / caps key, digits and a small set of symbols on a second layer, SPACE, backspace, CLEAR, CANCEL and OK. The page is the whole tab (1280 x 628); every key is at least 56 x 56 pixels and the
// rows use the whole width. Library palette and fonts, no glow. Used by the Macros page to name a macro (docs/ui-design/macro-names.md); the owner decides what OK means.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// In: begin (title, initial text, limit in characters, ids). Out: Request (okId) / Request (cancelId); the typed text is text () (the owner trims it and stores it on OK). Text is edited at its end (the
// caret is always there); the limit counts characters (UTF-8 code points); a key that would exceed it does nothing. A key acts when the finger comes up on it (a finger that slides off a key cancels).
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "core.h"
#include "theme.h"

namespace synthyboy_mpc { namespace ui {

class TextEntry : public Component
{
public:
    static constexpr int kBarH = 64, kFieldH = 64, kKeyH = 72, kGap = 8, kMargin = 16, kRows = 5, kCols = 20;   // the grid is kCols half-columns wide
    static constexpr int kMaxKeys = 64;
    enum class Layer : uint8_t { Letters, Symbols };
    enum class Shift : uint8_t { Off, Once, Caps };
    enum KeyKind : uint8_t { Char, ShiftKey, LayerKey, Backspace, Space, Clear, Cancel, Ok };

    struct Key
    {
        Rect r;
        KeyKind kind = Char;
        char ch = 0;   // the lower case character of a Char key
    };

    Size minSize() const override { return { 1280, kBarH + 4 + kFieldH + 12 + kRows * kKeyH + (kRows - 1) * kGap }; }

    /** Starts an entry: the title of the bar, the text so far (cut to the limit; the caret at its end), the longest text in characters, and the ids of the two Requests. A text that is empty starts with shift on
        (a name begins with a capital); the letter layer is the first one. */
    void begin (const char* title, const char* initial, int maxChars, int okId, int cancelId)
    {
        setText (title_, sizeof title_, title);
        maxChars_ = std::clamp (maxChars, 1, kMaxText / 4);
        text_.clear();
        chars_ = 0;
        for (const char* p = initial != nullptr ? initial : ""; *p != '\0' && chars_ < maxChars_;)
        {
            const size_t len = std::min (seqLen (static_cast<unsigned char> (*p)), std::strlen (p));
            text_.append (p, len);
            p += len;
            ++chars_;
        }
        okId_ = okId;
        cancelId_ = cancelId;
        layer_ = Layer::Letters;
        shift_ = text_.empty() ? Shift::Once : Shift::Off;
        pressed_ = -1;
        layout();
    }

    const std::string& text() const noexcept { return text_; }
    int chars() const noexcept { return chars_; }
    int maxChars() const noexcept { return maxChars_; }
    Layer layer() const noexcept { return layer_; }
    Shift shift() const noexcept { return shift_; }
    const char* title() const noexcept { return title_; }
    int numKeys() const noexcept { return numKeys_; }
    const Key& key (int i) const noexcept { return keys_[i]; }
    Rect fieldRect() const noexcept { return { rect_.x + kMargin, rect_.y + kBarH + 4, rect_.w - 2 * kMargin, kFieldH }; }

    /** The index of the key under (x, y), -1 for none. */
    int keyAt (int x, int y) const noexcept
    {
        for (int i = 0; i < numKeys_; ++i)
            if (keys_[i].r.contains (x, y))
                return i;
        return -1;
    }
    /** The key of a character on the current layer (-1: none); for the tests. */
    int keyOf (char c) const noexcept
    {
        const char lower = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
        for (int i = 0; i < numKeys_; ++i)
            if (keys_[i].kind == Char && keys_[i].ch == lower)
                return i;
        return -1;
    }
    int keyOfKind (KeyKind k) const noexcept
    {
        for (int i = 0; i < numKeys_; ++i)
            if (keys_[i].kind == k)
                return i;
        return -1;
    }
    /** Presses key i as a finger lifted on it: the text, the shift state, the layer, or the request of OK / CANCEL. */
    Change press (int i)
    {
        if (i < 0 || i >= numKeys_)
            return Change::none();
        const Key& k = keys_[i];
        switch (k.kind)
        {
            case Char:
            {
                if (chars_ >= maxChars_)
                    return Change::none();
                char c = k.ch;
                if (layer_ == Layer::Letters && c >= 'a' && c <= 'z' && shift_ != Shift::Off)
                    c = static_cast<char> (c - 'a' + 'A');
                text_ += c;
                ++chars_;
                if (shift_ == Shift::Once)
                    shift_ = Shift::Off;
                break;
            }
            case Space:
                if (chars_ < maxChars_)
                {
                    text_ += ' ';
                    ++chars_;
                    if (shift_ == Shift::Once)
                        shift_ = Shift::Off;
                }
                break;
            case Backspace:
                eraseLast();
                break;
            case Clear:
                text_.clear();
                chars_ = 0;
                shift_ = Shift::Once;
                break;
            case ShiftKey:
                shift_ = shift_ == Shift::Off ? Shift::Once : (shift_ == Shift::Once ? Shift::Caps : Shift::Off);   // tap: next letter a capital; again: caps lock; again: off
                break;
            case LayerKey:
                layer_ = layer_ == Layer::Letters ? Layer::Symbols : Layer::Letters;
                layout();
                break;
            case Cancel: return Change::request (cancelId_);
            case Ok: return Change::request (okId_);
        }
        return Change::none();
    }

protected:
    static constexpr int kMaxText = 256;

    void layout() { buildKeys(); }
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        p.line (static_cast<float> (rect_.x + 8), static_cast<float> (rect_.y + kBarH - 2), static_cast<float> (rect_.right() - 8), static_cast<float> (rect_.y + kBarH - 2), 1.5f, p.c (T.keyline, 255));
        p.textLeft ({ rect_.x + 14, rect_.y + 4, rect_.w - 28, kBarH - 8 }, title_, T.fTitle, p.c (T.text, 255), T.fMin);

        // the field: the text, the caret at its end, the count
        const Rect f = fieldRect();
        p.control (f, false, true);
        const float size = T.fValue + 4.0f;
        const Rect inner { f.x + 16, f.y + 6, f.w - 32 - 90, f.h - 12 };
        float used = 0.0f;
        if (! text_.empty())
            used = Pen::textWidth (text_.c_str(), Pen::fitSize (text_.c_str(), static_cast<float> (inner.w), static_cast<float> (inner.h), size, T.fMin));
        if (! text_.empty())
            p.textLeft (inner, text_.c_str(), size, p.c (T.text, 255), T.fMin);
        const int cx = inner.x + std::min (static_cast<int> (std::ceil (used)) + 3, inner.w);
        p.line (static_cast<float> (cx), static_cast<float> (f.y + 12), static_cast<float> (cx), static_cast<float> (f.bottom() - 12), 2.0f, p.c (T.flow[0], 255));
        char count[16];
        std::snprintf (count, sizeof count, "%d/%d", chars_, maxChars_);
        p.textRight ({ f.right() - 90, f.y + 6, 76, f.h - 12 }, count, T.fLabel, p.c (chars_ >= maxChars_ ? T.flow[0] : T.textFaint, 255), T.fMin);

        for (int i = 0; i < numKeys_; ++i)
            drawKey (p, i);
    }

    Change onDown (int x, int y, double) override
    {
        pressed_ = keyAt (x, y);
        return Change::none();
    }
    Change onMove (int x, int y) override
    {
        if (pressed_ >= 0 && ! keys_[pressed_].r.contains (x, y))
            pressed_ = -1;   // the finger slid off the key: it does nothing
        return Change::none();
    }
    Change onUp (int x, int y) override
    {
        const int k = pressed_;
        pressed_ = -1;
        return k >= 0 && keys_[k].r.contains (x, y) ? press (k) : Change::none();
    }

private:
    static size_t seqLen (unsigned char c) noexcept { return c < 0x80 ? 1 : (c >= 0xf0 ? 4 : (c >= 0xe0 ? 3 : (c >= 0xc0 ? 2 : 1))); }
    static int countChars (const std::string& s) noexcept
    {
        int n = 0;
        for (const char c : s)
            n += (static_cast<unsigned char> (c) & 0xc0u) != 0x80u;
        return n;
    }
    void eraseLast()
    {
        if (text_.empty())
            return;   // backspace at the start does nothing
        size_t at = text_.size() - 1;
        while (at > 0 && (static_cast<unsigned char> (text_[at]) & 0xc0u) == 0x80u)
            --at;
        text_.erase (at);
        --chars_;
    }

    void add (int col0, int col1, int row, KeyKind kind, char ch = 0)
    {
        if (numKeys_ >= kMaxKeys)
            return;
        const int gridW = rect_.w - 2 * kMargin;
        const int x0 = rect_.x + kMargin + col0 * gridW / kCols, x1 = rect_.x + kMargin + col1 * gridW / kCols;
        const int top = rect_.y + kBarH + 4 + kFieldH + 12 + row * (kKeyH + kGap);
        Key& k = keys_[numKeys_++];
        k.r = { x0 + kGap / 2, top, x1 - x0 - kGap, kKeyH };
        k.kind = kind;
        k.ch = ch;
    }
    void addRow (const char* chars, int col, int row)
    {
        for (const char* c = chars; *c != '\0'; ++c, col += 2)
            add (col, col + 2, row, Char, *c);
    }
    void buildKeys()
    {
        numKeys_ = 0;
        if (layer_ == Layer::Letters)
        {
            addRow ("qwertyuiop", 0, 0);
            addRow ("asdfghjkl", 1, 1);
            add (0, 3, 2, ShiftKey);
            addRow ("zxcvbnm", 3, 2);
            add (17, 20, 2, Backspace);
            add (0, 4, 3, LayerKey);
        }
        else
        {
            addRow ("1234567890", 0, 0);
            addRow ("-+/&'()#%", 1, 1);
            add (0, 3, 2, Char, '_');
            addRow (".,!?:*=", 3, 2);
            add (17, 20, 2, Backspace);
            add (0, 4, 3, LayerKey);
        }
        add (4, 16, 3, Space);
        add (16, 20, 3, Clear);
        add (0, 10, 4, Cancel);
        add (10, 20, 4, Ok);
    }

    void drawKey (Pen& p, int i) const
    {
        const theme::Look& T = theme::L();
        const Key& k = keys_[i];
        const bool down = pressed_ == i;
        const bool lit = (k.kind == ShiftKey && shift_ != Shift::Off) || (k.kind == LayerKey && layer_ == Layer::Symbols);
        const bool ok = k.kind == Ok;
        const Rect b = k.r;
        if (ok || lit || down)
        {
            p.rrect (b, T.rControl, p.c (ok ? T.flow[0] : (down ? T.keylineHi : T.flow[0]), 255));
            p.rrect (b, T.rControl, p.c (T.keylineHi, 255), T.frame);
        }
        else
        {
            p.control (b, false, false);
        }
        const uint32_t ink = p.c (ok || (lit && ! down) ? T.onBright : T.text, 255);
        char label[16] = {};
        switch (k.kind)
        {
            case Char:
                label[0] = (layer_ == Layer::Letters && k.ch >= 'a' && k.ch <= 'z' && shift_ != Shift::Off) ? static_cast<char> (k.ch - 'a' + 'A') : k.ch;
                p.textCentered (b.inset (4), label, T.fValue + 4.0f, ink, T.fMin);
                break;
            case Space: p.textCentered (b.inset (4), "SPACE", T.fLabel + 2.0f, ink, T.fMin); break;
            case Clear: p.textCentered (b.inset (4), "CLEAR", T.fLabel + 2.0f, ink, T.fMin); break;
            case Cancel: p.textCentered (b.inset (4), "CANCEL", T.fLabel + 4.0f, ink, T.fMin); break;
            case Ok: p.textCentered (b.inset (4), "OK", T.fLabel + 4.0f, ink, T.fMin); break;
            case LayerKey: p.textCentered (b.inset (4), layer_ == Layer::Letters ? "?123" : "ABC", T.fLabel + 2.0f, ink, T.fMin); break;
            case ShiftKey:
            {
                // an up arrow (an outline); caps lock: the arrow over a bar
                const float cx = static_cast<float> (b.x) + static_cast<float> (b.w) * 0.5f, cy = static_cast<float> (b.y) + static_cast<float> (b.h) * 0.5f - (shift_ == Shift::Caps ? 3.0f : 0.0f);
                p.style (1.0f, true);
                p.line (cx, cy + 12.0f, cx, cy - 12.0f, 3.0f, ink);
                p.line (cx - 11.0f, cy - 1.0f, cx, cy - 12.0f, 3.0f, ink);
                p.line (cx + 11.0f, cy - 1.0f, cx, cy - 12.0f, 3.0f, ink);
                if (shift_ == Shift::Caps)
                    p.line (cx - 11.0f, cy + 19.0f, cx + 11.0f, cy + 19.0f, 3.0f, ink);
                p.plain();
                break;
            }
            case Backspace:
            {
                // a left arrow
                const float cx = static_cast<float> (b.x) + static_cast<float> (b.w) * 0.5f, cy = static_cast<float> (b.y) + static_cast<float> (b.h) * 0.5f;
                p.style (1.0f, true);
                p.line (cx + 16.0f, cy, cx - 14.0f, cy, 3.0f, ink);
                p.line (cx - 14.0f, cy, cx - 3.0f, cy - 11.0f, 3.0f, ink);
                p.line (cx - 14.0f, cy, cx - 3.0f, cy + 11.0f, 3.0f, ink);
                p.plain();
                break;
            }
        }
    }

    char title_[40] = {};
    std::string text_;
    int chars_ = 0, maxChars_ = 16, okId_ = 0, cancelId_ = 0;
    Layer layer_ = Layer::Letters;
    Shift shift_ = Shift::Off;
    Key keys_[kMaxKeys];
    int numKeys_ = 0, pressed_ = -1;

public:
    void place (const Rect& r) override
    {
        Component::place (r);
        buildKeys();
    }
};

}} // namespace synthyboy_mpc::ui
