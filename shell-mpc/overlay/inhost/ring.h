// SynthyBoy - MPC in-host drawing (experimental): the hand-over of the finished picture from the overlay thread (the producer) to the readers (the hook on the host's main thread, the front-buffer writer).
// A ring of four picture buffers, one producer, up to two readers, no lock and no wait: the producer writes into a buffer that is neither the latest nor held by a reader and then makes it the latest with one
// atomic store; a reader takes the latest by counting itself in and checking that it is still the latest (the producer never writes a buffer with a reader count over 0 that it could not have seen as latest:
// both sides use sequentially consistent operations). A reader therefore always sees one COMPLETE picture, whatever the producer does meanwhile: no tearing between the writes of the one and the copy of the other.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <memory>

#include "blit.h"

namespace synthyboy_mpc { namespace overlay { namespace inhost {

struct PictureMeta
{
    int view = -1, slot = -1, rotation = 90;
    Rect panel;
    uint32_t gen = 0;
};

class FrameRing
{
public:
    static constexpr int kBuffers = 4;

    struct Hold
    {
        PictureRef pic;
        PictureMeta meta;
        int index = -1;
    };

    FrameRing() = default;
    FrameRing (const FrameRing&) = delete;
    FrameRing& operator= (const FrameRing&) = delete;

    /** Memory for pictures of up to maxPixels pixels and maxRows rows (once; the producer thread, while no reader can be running: before the hook is armed or after it was removed). */
    bool allocate (size_t maxPixels, int maxRows)
    {
        release();
        for (int i = 0; i < kBuffers; ++i)
        {
            pix_[i].reset (new (std::nothrow) uint32_t[maxPixels]);
            kinds_[i].reset (new (std::nothrow) uint8_t[static_cast<size_t> (maxRows)]);
            if (! pix_[i] || ! kinds_[i])
            {
                release();
                return false;
            }
        }
        maxPixels_ = maxPixels;
        maxRows_ = maxRows;
        return true;
    }
    /** Frees the memory (the same condition as allocate). */
    void release() noexcept
    {
        latest_.store (-1);
        for (int i = 0; i < kBuffers; ++i)
        {
            pix_[i].reset();
            kinds_[i].reset();
        }
        maxPixels_ = 0;
        maxRows_ = 0;
        writing_ = -1;
    }
    bool allocated() const noexcept { return maxPixels_ != 0; }
    /** The geometry of the next pictures (the producer). */
    bool configure (int w, int h) noexcept
    {
        if (w <= 0 || h <= 0 || static_cast<size_t> (w) * static_cast<size_t> (h) > maxPixels_ || h > maxRows_)
            return false;
        w_ = w;
        h_ = h;
        return true;
    }
    int width() const noexcept { return w_; }
    int height() const noexcept { return h_; }

    /** The buffer to draw the next picture into (w x h, tightly packed): one that is neither the latest nor held by a reader. nullptr: none is free (a reader is slow; the picture is skipped). The producer only. */
    uint32_t* writeBuffer (size_t& strideBytes) noexcept
    {
        if (! allocated() || w_ <= 0)
            return nullptr;
        const int lat = latest_.load();
        for (int j = 0; j < kBuffers; ++j)
            if (j != lat && readers_[j].load() == 0)
            {
                writing_ = j;
                strideBytes = static_cast<size_t> (w_) * 4u;
                return pix_[j].get();
            }
        writing_ = -1;
        return nullptr;
    }
    /** The buffer given out by writeBuffer is complete: it becomes the latest. The producer only. */
    void publish (int view, int slot, int rotation, const Rect& panel) noexcept
    {
        if (writing_ < 0)
            return;
        classifyRows (pix_[writing_].get(), w_, h_, kinds_[writing_].get());
        PictureMeta& m = meta_[writing_];
        m.view = view;
        m.slot = slot;
        m.rotation = rotation;
        m.panel = panel;
        m.gen = gen_.fetch_add (1) + 1u;
        latest_.store (writing_);
        writing_ = -1;
    }
    /** The buffer given out by writeBuffer and not yet published (the producer's own: the plane path takes its content over when the in-host path gives up). */
    const uint32_t* currentWrite (size_t& strideBytes) const noexcept
    {
        if (writing_ < 0 || ! allocated())
            return nullptr;
        strideBytes = static_cast<size_t> (w_) * 4u;
        return pix_[writing_].get();
    }
    /** The latest picture, or none: the readers' side. The hold must be released. */
    bool acquire (Hold& h) noexcept
    {
        for (int tries = 0; tries < 8; ++tries)
        {
            const int i = latest_.load();
            if (i < 0)
                return false;
            readers_[i].fetch_add (1);
            if (latest_.load() == i)
            {
                h.index = i;
                h.meta = meta_[i];
                h.pic.pix = pix_[i].get();
                h.pic.kinds = kinds_[i].get();
                h.pic.w = w_;
                h.pic.h = h_;
                h.pic.panel = h.meta.panel;
                return true;
            }
            readers_[i].fetch_sub (1);
        }
        return false;
    }
    void release (Hold& h) noexcept
    {
        if (h.index >= 0)
            readers_[h.index].fetch_sub (1);
        h.index = -1;
    }
    /** No picture is shown any more (the view went, the overlay is hidden): readers find none. */
    void invalidate() noexcept { latest_.store (-1); }
    bool hasPicture() const noexcept { return latest_.load() >= 0; }
    uint32_t generation() const noexcept { return gen_.load(); }
    int readersNow() const noexcept
    {
        int n = 0;
        for (int i = 0; i < kBuffers; ++i)
            n += readers_[i].load();
        return n;
    }

private:
    std::unique_ptr<uint32_t[]> pix_[kBuffers];
    std::unique_ptr<uint8_t[]> kinds_[kBuffers];
    PictureMeta meta_[kBuffers];
    std::atomic<int> readers_[kBuffers] = {};
    std::atomic<int> latest_ { -1 };
    size_t maxPixels_ = 0;
    int maxRows_ = 0, w_ = 0, h_ = 0, writing_ = -1;
    std::atomic<uint32_t> gen_ { 0 };
};

}}} // namespace synthyboy_mpc::overlay::inhost
