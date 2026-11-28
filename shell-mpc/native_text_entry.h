// SynthyBoy - MPC native text entry: the plain data behind the skin keyboard (components/native/keyboard). The skin buttons write a key code into the plugin parameter "TextEdit Key"; this header holds
// the text buffer, the caret rules and the OK / CANCEL logic (Model), and the bridge from the host thread that receives the codes to the overlay thread that owns the page (KeyPort).
// Layers (lower / UPPER / symbols) live in the skin (Local/3/layer): the plugin only ever sees the character a key stands for.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <string>

namespace synthyboy_mpc::textentry
{
// The key codes of the parameter: 0 idle, 1..95 the printable ASCII characters 32..126 (code = character - 31), then the four actions.
constexpr int kKeyIdle = 0, kKeyFirstChar = 1, kKeyLastChar = 95, kKeyBackspace = 96, kKeyClear = 97, kKeyOk = 98, kKeyCancel = 99;
// The contexts of the parameter: 0 closed, 1..8 the name of macro 1..8, then the LFO shape names.
constexpr int kCtxClosed = 0, kCtxMacroFirst = 1, kCtxMacroLast = 8, kCtxShapeSaveAs = 9, kCtxShapeRename = 10;
/** The host delivers one tap as two press events 30..140 ms apart with no release between (Key 61 measurement): the same key inside this window is one press. */
constexpr double kDebounceMs = 250.0;

/** The key code of the host value of the parameter (0..1 over kTextEditKeys steps); 0 for anything out of range or not finite. */
inline int keyOfValue (float value, int steps = 100) noexcept
{
    if (!(value > 0.0f) || value > 1.0f)
        return kKeyIdle;
    return std::clamp (static_cast<int> (value * static_cast<float> (steps - 1) + 0.5f), 0, steps - 1);
}

/** The value of a context (0 .. 10) as the host parameter. */
inline float valueOfContext (int context, int steps = 11) noexcept
{
    return static_cast<float> (std::clamp (context, 0, steps - 1)) / static_cast<float> (steps - 1);
}
inline int contextOfValue (float value, int steps = 11) noexcept
{
    return !(value > 0.0f) ? 0 : std::clamp (static_cast<int> (value * static_cast<float> (steps - 1) + 0.5f), 0, steps - 1);
}

enum class Outcome { None, Edited, Ok, Cancel };

/** The text being typed: ASCII only (the keys are), edited at its end, at most `maxChars` characters; a key that would exceed the limit does nothing. */
class Model
{
public:
    void begin (const char* title, const char* initial, int maxChars)
    {
        title_ = title != nullptr ? title : "";
        maxChars_ = std::clamp (maxChars, 1, 200);
        text_.clear();
        for (const char* p = initial != nullptr ? initial : ""; *p != '\0' && static_cast<int> (text_.size()) < maxChars_; ++p)
            if (static_cast<unsigned char> (*p) >= 32 && static_cast<unsigned char> (*p) <= 126)
                text_ += *p;
        open_ = true;
    }
    void close() noexcept { open_ = false; }
    bool open() const noexcept { return open_; }
    const std::string& text() const noexcept { return text_; }
    const std::string& title() const noexcept { return title_; }
    int maxChars() const noexcept { return maxChars_; }

    Outcome press (int key)
    {
        if (!open_ || key <= kKeyIdle)
            return Outcome::None;
        if (key == kKeyOk)
            return Outcome::Ok;
        if (key == kKeyCancel)
            return Outcome::Cancel;
        if (key == kKeyClear)
        {
            if (text_.empty())
                return Outcome::None;
            text_.clear();
            return Outcome::Edited;
        }
        if (key == kKeyBackspace)
        {
            if (text_.empty())
                return Outcome::None;
            text_.pop_back();
            return Outcome::Edited;
        }
        if (key < kKeyFirstChar || key > kKeyLastChar || static_cast<int> (text_.size()) >= maxChars_)
            return Outcome::None;
        text_ += static_cast<char> (key + 31);
        return Outcome::Edited;
    }

    /** The text with the caret ("|") for a Label whose display string holds `cap` bytes (the NUL included): a text that does not fit shows its end behind "...". */
    std::string display (size_t cap) const
    {
        const size_t room = cap > 1 ? cap - 1 : 0;
        std::string shown = text_ + '|';
        if (shown.size() <= room)
            return shown;
        if (room <= 3)
            return shown.substr (shown.size() - room);
        return "..." + shown.substr (shown.size() - (room - 3));
    }

private:
    std::string title_, text_;
    int maxChars_ = 16;
    bool open_ = false;
};

/** Host thread -> overlay thread: the key codes written by the host (post), debounced per key, taken in order by the page (take). */
class KeyPort
{
public:
    /** `nowMs`: a steady clock; a key repeated inside kDebounceMs of its own last accepted press is dropped. */
    void post (int key, double nowMs) noexcept
    {
        if (key < kKeyFirstChar || key > kKeyCancel)
            return;
        std::lock_guard<std::mutex> g (mutex_);
        if (lastMs_[key] > 0.0 && nowMs - lastMs_[key] < kDebounceMs && nowMs >= lastMs_[key])
            return;
        lastMs_[key] = nowMs;
        if (count_ < kRing)
            ring_[(head_ + count_++) % kRing] = key;
    }
    void post (int key) noexcept
    {
        post (key, std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    /** The oldest key not taken yet, 0 when there is none. */
    int take() noexcept
    {
        std::lock_guard<std::mutex> g (mutex_);
        if (count_ == 0)
            return kKeyIdle;
        const int key = ring_[head_];
        head_ = (head_ + 1) % kRing;
        --count_;
        return key;
    }
    /** Forgets what is queued and the debounce times (a keyboard opens or closes). */
    void clear() noexcept
    {
        std::lock_guard<std::mutex> g (mutex_);
        count_ = head_ = 0;
        std::memset (lastMs_, 0, sizeof lastMs_);
    }

private:
    static constexpr int kRing = 32;
    std::mutex mutex_;
    int ring_[kRing] {};
    int head_ = 0, count_ = 0;
    double lastMs_[100] {};
};
}
