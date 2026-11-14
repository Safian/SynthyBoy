// SynthyBoy - MPC overlay: multitouch protocol B decoder and coordinate mapping. Pure C++17, no OS headers.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace synthyboy_mpc { namespace overlay {

/** A touch event emitted at upright UI pixel coordinates. */
struct TouchEvent
{
    enum Kind
    {
        Down,
        Move,
        Up
    };

    Kind kind = Down;
    int x = 0;          // upright UI pixels
    int y = 0;
    uint64_t timeUs = 0;

    constexpr bool operator== (const TouchEvent& o) const noexcept
    {
        return kind == o.kind && x == o.x && y == o.y && timeUs == o.timeUs;
    }
    constexpr bool operator!= (const TouchEvent& o) const noexcept
    {
        return ! (*this == o);
    }
};

/** Multitouch protocol B decoder with coordinate mapping for upright display. Pure C++17, no heap allocations, no locks, no exceptions. */
class TouchDecoder
{
public:
    // Linux evdev protocol constants defined here so this header requires no OS headers.
    static constexpr uint16_t kEvSyn = 0x00;
    static constexpr uint16_t kEvKey = 0x01;
    static constexpr uint16_t kEvAbs = 0x03;

    static constexpr uint16_t kSynReport   = 0;
    static constexpr uint16_t kSynDropped  = 3;

    static constexpr uint16_t kAbsMtSlot        = 0x2f; // 47
    static constexpr uint16_t kAbsMtPositionX   = 0x35; // 53
    static constexpr uint16_t kAbsMtPositionY   = 0x36; // 54
    static constexpr uint16_t kAbsMtTrackingId  = 0x39; // 57

    static constexpr int kMaxSlots = 32;
    static constexpr size_t kQueueCapacity = 16;

    TouchDecoder() = default;

    TouchDecoder (int panelW, int panelH, int rotation = 90) noexcept
        : panelW_ (panelW), panelH_ (panelH),
          minX_ (0), maxX_ (panelW > 0 ? panelW - 1 : 0),
          minY_ (0), maxY_ (panelH > 0 ? panelH - 1 : 0),
          rotation_ (normaliseRotation (rotation))
    {
    }

    void setPanelSize (int w, int h) noexcept
    {
        panelW_ = w;
        panelH_ = h;
        minX_ = 0;
        maxX_ = w > 0 ? w - 1 : 0;
        minY_ = 0;
        maxY_ = h > 0 ? h - 1 : 0;
    }

    void setRanges (int minX, int maxX, int minY, int maxY) noexcept
    {
        minX_ = minX;
        maxX_ = maxX;
        minY_ = minY;
        maxY_ = maxY;
        panelW_ = (maxX >= minX) ? (maxX - minX + 1) : 0;
        panelH_ = (maxY >= minY) ? (maxY - minY + 1) : 0;
    }

    void setRotation (int degrees) noexcept
    {
        rotation_ = normaliseRotation (degrees);
    }

    int rotation() const noexcept { return rotation_; }
    int panelW() const noexcept { return panelW_; }
    int panelH() const noexcept { return panelH_; }
    int minX() const noexcept { return minX_; }
    int maxX() const noexcept { return maxX_; }
    int minY() const noexcept { return minY_; }
    int maxY() const noexcept { return maxY_; }

    bool isDown() const noexcept { return trackedSlot_ >= 0; }
    int trackedSlot() const noexcept { return trackedSlot_; }
    int32_t trackedTrackingId() const noexcept { return trackedTrackingId_; }

    /** Feed a single raw evdev event. Any resulting TouchEvent is stored in the internal queue. */
    void feed (uint16_t type, uint16_t code, int32_t value, uint64_t timeUs = 0) noexcept
    {
        processEvent (type, code, value, timeUs, [this] (const TouchEvent& ev) {
            enqueue (ev);
        });
    }

    /** Feed a raw evdev event and invoke sink (const TouchEvent&) for any emitted event. */
    template <typename Sink>
    void feed (uint16_t type, uint16_t code, int32_t value, uint64_t timeUs, Sink&& sink)
    {
        processEvent (type, code, value, timeUs, [&] (const TouchEvent& ev) {
            enqueue (ev);
            sink (ev);
        });
    }

    /** Feed a raw evdev event. If this event completes a TouchEvent, returns true and copies it into out. */
    bool feed (uint16_t type, uint16_t code, int32_t value, uint64_t timeUs, TouchEvent& out) noexcept
    {
        const size_t prev = qCount_;
        feed (type, code, value, timeUs);
        if (qCount_ > prev)
            return pollEvent (out);
        return false;
    }

    /** Pops the next TouchEvent from the internal queue, if any. Returns false when empty. */
    bool pollEvent (TouchEvent& out) noexcept
    {
        if (qCount_ == 0)
            return false;
        out = queue_[qHead_];
        qHead_ = (qHead_ + 1) % kQueueCapacity;
        --qCount_;
        return true;
    }

    size_t available() const noexcept { return qCount_; }

    void clearEvents() noexcept
    {
        qHead_ = 0;
        qTail_ = 0;
        qCount_ = 0;
    }

    /** Resets touch tracking state. Emits Up if a finger was tracked. */
    void reset (uint64_t timeUs = 0) noexcept
    {
        if (trackedSlot_ >= 0 && hasLastUiCoords_)
        {
            enqueue (TouchEvent { TouchEvent::Up, lastUiX_, lastUiY_, timeUs });
        }
        trackedSlot_ = -1;
        trackedTrackingId_ = -1;
        currentSlot_ = 0;
        hasLastUiCoords_ = false;
        trackedDownPending_ = false;
        trackedUpPending_ = false;
        trackedReplaced_ = false;
        oldTrackingId_ = -1;
        trackedCoordsChanged_ = false;

        for (int i = 0; i < kMaxSlots; ++i)
            slots_[i] = Slot{};
    }

    /** Map panel coordinates (sx, sy) to upright UI pixels (uiX, uiY) under current rotation and clamp. */
    void mapPanelToUi (int sx, int sy, int& uiX, int& uiY) const noexcept
    {
        // Clamp to device ABS ranges
        if (sx < minX_) sx = minX_;
        if (sx > maxX_) sx = maxX_;
        if (sy < minY_) sy = minY_;
        if (sy > maxY_) sy = maxY_;

        const int rx = sx - minX_;
        const int ry = sy - minY_;
        const int W = panelW_;
        const int H = panelH_;

        switch (rotation_)
        {
            case 0:
                uiX = rx;
                uiY = ry;
                break;
            case 180:
                uiX = (W > 0) ? (W - 1 - rx) : 0;
                uiY = (H > 0) ? (H - 1 - ry) : 0;
                break;
            case 270:
                uiX = ry;
                uiY = (W > 0) ? (W - 1 - rx) : 0;
                break;
            case 90:
            default:
                uiX = (H > 0) ? (H - 1 - ry) : 0;
                uiY = rx;
                break;
        }
    }

    static constexpr int normaliseRotation (int degrees) noexcept
    {
        return (degrees == 0 || degrees == 90 || degrees == 180 || degrees == 270) ? degrees : 90;
    }

private:
    struct Slot
    {
        int32_t trackingId = -1;
        int32_t x = 0;
        int32_t y = 0;
        bool hasX = false;
        bool hasY = false;
    };

    template <typename EmitFn>
    void processEvent (uint16_t type, uint16_t code, int32_t value, uint64_t timeUs, EmitFn&& emit)
    {
        if (type == kEvSyn)
        {
            if (code == kSynDropped)
            {
                // SYN_DROPPED: state is lost; emit Up if a finger was tracked and resynchronise from next complete frame
                if (trackedSlot_ >= 0 && hasLastUiCoords_)
                {
                    emit (TouchEvent { TouchEvent::Up, lastUiX_, lastUiY_, timeUs });
                }
                trackedSlot_ = -1;
                trackedTrackingId_ = -1;
                hasLastUiCoords_ = false;
                trackedDownPending_ = false;
                trackedUpPending_ = false;
                trackedReplaced_ = false;
                oldTrackingId_ = -1;
                trackedCoordsChanged_ = false;

                for (int i = 0; i < kMaxSlots; ++i)
                    slots_[i] = Slot{};

                return;
            }

            if (code == kSynReport)
            {
                // Process frame changes for the tracked finger
                if (trackedReplaced_)
                {
                    // A tracking id change without an Up: emit Up for old touch, Down for new touch
                    emit (TouchEvent { TouchEvent::Up, lastUiX_, lastUiY_, timeUs });

                    if (trackedSlot_ >= 0)
                    {
                        int newX = 0, newY = 0;
                        mapPanelToUi (slots_[trackedSlot_].x, slots_[trackedSlot_].y, newX, newY);
                        emit (TouchEvent { TouchEvent::Down, newX, newY, timeUs });
                        lastUiX_ = newX;
                        lastUiY_ = newY;
                        hasLastUiCoords_ = true;
                    }
                    trackedReplaced_ = false;
                    trackedDownPending_ = false;
                    trackedUpPending_ = false;
                    trackedCoordsChanged_ = false;
                    return;
                }

                if (trackedUpPending_)
                {
                    if (trackedCoordsChanged_ && trackedSlot_ >= 0)
                    {
                        mapPanelToUi (slots_[trackedSlot_].x, slots_[trackedSlot_].y, lastUiX_, lastUiY_);
                    }
                    emit (TouchEvent { TouchEvent::Up, lastUiX_, lastUiY_, timeUs });
                    trackedSlot_ = -1;
                    trackedTrackingId_ = -1;
                    hasLastUiCoords_ = false;
                    trackedUpPending_ = false;
                    trackedDownPending_ = false;
                    trackedCoordsChanged_ = false;
                    return;
                }

                if (trackedDownPending_)
                {
                    if (trackedSlot_ >= 0)
                    {
                        int uiX = 0, uiY = 0;
                        mapPanelToUi (slots_[trackedSlot_].x, slots_[trackedSlot_].y, uiX, uiY);
                        emit (TouchEvent { TouchEvent::Down, uiX, uiY, timeUs });
                        lastUiX_ = uiX;
                        lastUiY_ = uiY;
                        hasLastUiCoords_ = true;
                    }
                    trackedDownPending_ = false;
                    trackedCoordsChanged_ = false;
                    return;
                }

                if (trackedCoordsChanged_)
                {
                    if (trackedSlot_ >= 0)
                    {
                        int uiX = 0, uiY = 0;
                        mapPanelToUi (slots_[trackedSlot_].x, slots_[trackedSlot_].y, uiX, uiY);
                        if (! hasLastUiCoords_ || uiX != lastUiX_ || uiY != lastUiY_)
                        {
                            emit (TouchEvent { TouchEvent::Move, uiX, uiY, timeUs });
                            lastUiX_ = uiX;
                            lastUiY_ = uiY;
                            hasLastUiCoords_ = true;
                        }
                    }
                    trackedCoordsChanged_ = false;
                    return;
                }

                return;
            }
        }
        else if (type == kEvAbs)
        {
            if (code == kAbsMtSlot)
            {
                if (value >= 0 && value < kMaxSlots)
                    currentSlot_ = value;
                return;
            }

            if (currentSlot_ < 0 || currentSlot_ >= kMaxSlots)
                return;

            Slot& slot = slots_[currentSlot_];

            if (code == kAbsMtTrackingId)
            {
                const int32_t prevId = slot.trackingId;
                slot.trackingId = value;

                if (trackedSlot_ < 0)
                {
                    // No finger tracked: the first slot that went down becomes the tracked finger
                    if (prevId < 0 && value >= 0)
                    {
                        trackedSlot_ = currentSlot_;
                        trackedTrackingId_ = value;
                        trackedDownPending_ = true;
                        trackedUpPending_ = false;
                        trackedReplaced_ = false;
                        trackedCoordsChanged_ = false;
                    }
                }
                else if (currentSlot_ == trackedSlot_)
                {
                    // Tracked finger changed state
                    if (value < 0)
                    {
                        trackedUpPending_ = true;
                        trackedDownPending_ = false;
                        trackedReplaced_ = false;
                    }
                    else if (value != trackedTrackingId_)
                    {
                        // Tracking ID changed without an Up
                        trackedReplaced_ = true;
                        oldTrackingId_ = trackedTrackingId_;
                        trackedTrackingId_ = value;
                    }
                }
                return;
            }

            if (code == kAbsMtPositionX)
            {
                slot.x = value;
                slot.hasX = true;
                if (currentSlot_ == trackedSlot_)
                    trackedCoordsChanged_ = true;
                return;
            }

            if (code == kAbsMtPositionY)
            {
                slot.y = value;
                slot.hasY = true;
                if (currentSlot_ == trackedSlot_)
                    trackedCoordsChanged_ = true;
                return;
            }
        }
    }

    void enqueue (const TouchEvent& ev) noexcept
    {
        if (qCount_ < kQueueCapacity)
        {
            queue_[qTail_] = ev;
            qTail_ = (qTail_ + 1) % kQueueCapacity;
            ++qCount_;
        }
    }

    int panelW_ = 800;
    int panelH_ = 1280;
    int minX_ = 0;
    int maxX_ = 799;
    int minY_ = 0;
    int maxY_ = 1279;
    int rotation_ = 90;

    int currentSlot_ = 0;
    Slot slots_[kMaxSlots] = {};

    int trackedSlot_ = -1;
    int32_t trackedTrackingId_ = -1;

    int lastUiX_ = 0;
    int lastUiY_ = 0;
    bool hasLastUiCoords_ = false;

    bool trackedDownPending_ = false;
    bool trackedUpPending_ = false;
    bool trackedReplaced_ = false;
    int32_t oldTrackingId_ = -1;
    bool trackedCoordsChanged_ = false;

    TouchEvent queue_[kQueueCapacity] = {};
    size_t qHead_ = 0;
    size_t qTail_ = 0;
    size_t qCount_ = 0;
};

}} // namespace synthyboy_mpc::overlay
