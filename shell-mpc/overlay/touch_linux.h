// SynthyBoy - MPC overlay: Linux evdev touch reader (EVIOCGBIT, EVIOCGPROP, EVIOCGABS).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <poll.h>

#if defined(__linux__)
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>
#include <ctime>
#else
#include <sys/time.h>
#include <fcntl.h>

#ifndef EV_SYN
#define EV_SYN 0x00
#endif
#ifndef EV_KEY
#define EV_KEY 0x01
#endif
#ifndef EV_ABS
#define EV_ABS 0x03
#endif

#ifndef SYN_REPORT
#define SYN_REPORT 0
#endif
#ifndef SYN_DROPPED
#define SYN_DROPPED 3
#endif

#ifndef ABS_MT_SLOT
#define ABS_MT_SLOT 0x2f
#endif
#ifndef ABS_MT_POSITION_X
#define ABS_MT_POSITION_X 0x35
#endif
#ifndef ABS_MT_POSITION_Y
#define ABS_MT_POSITION_Y 0x36
#endif
#ifndef ABS_MT_TRACKING_ID
#define ABS_MT_TRACKING_ID 0x39
#endif

#ifndef INPUT_PROP_DIRECT
#define INPUT_PROP_DIRECT 0x01
#endif

#ifndef O_CLOEXEC
#define O_CLOEXEC 0x1000000
#endif

struct input_event
{
    struct timeval time;
    uint16_t type;
    uint16_t code;
    int32_t value;
};

struct input_absinfo
{
    int32_t value;
    int32_t minimum;
    int32_t maximum;
    int32_t fuzz;
    int32_t flat;
    int32_t resolution;
};

#define EVIOCGPROP(len) (0x80004509u | ((static_cast<uint32_t>(len) & 0x1fffu) << 16))
#define EVIOCGBIT(ev, len) (0x80004520u | (static_cast<uint32_t>(ev) & 0xffu) | ((static_cast<uint32_t>(len) & 0x1fffu) << 16))
#define EVIOCGABS(abs) (0x80004540u | (static_cast<uint32_t>(abs) & 0x3fu) | ((static_cast<uint32_t>(sizeof(struct input_absinfo)) & 0x1fffu) << 16))
#endif

#include "touch_decoder.h"
#include "touch_input.h"

namespace synthyboy_mpc { namespace overlay {

/** Syscall interface for evdev touch reading and polling, allowing host testing with fakes. */
class ITouchSyscalls
{
public:
    virtual ~ITouchSyscalls() = default;
    virtual int open (const char* path, int flags) = 0;
    virtual int close (int fd) = 0;
    virtual ssize_t read (int fd, void* buf, size_t count) = 0;
    virtual int ioctl (int fd, unsigned long request, void* arg) = 0;
    virtual int poll (struct pollfd* fds, nfds_t nfds, int timeoutMs) = 0;
    virtual uint64_t nowMs() = 0;
};

#if defined(__linux__)
class LinuxTouchSyscalls final : public ITouchSyscalls
{
public:
    int open (const char* path, int flags) override
    {
        return ::open (path, flags);
    }

    int close (int fd) override
    {
        return ::close (fd);
    }

    ssize_t read (int fd, void* buf, size_t count) override
    {
        return ::read (fd, buf, count);
    }

    int ioctl (int fd, unsigned long request, void* arg) override
    {
        return ::ioctl (fd, request, arg);
    }

    int poll (struct pollfd* fds, nfds_t nfds, int timeoutMs) override
    {
        return ::poll (fds, nfds, timeoutMs);
    }

    uint64_t nowMs() override
    {
        timespec ts;
        clock_gettime (CLOCK_MONOTONIC, &ts);
        return static_cast<uint64_t> (ts.tv_sec) * 1000ULL +
               static_cast<uint64_t> (ts.tv_nsec / 1000000L);
    }

    static LinuxTouchSyscalls& instance() noexcept
    {
        static LinuxTouchSyscalls s;
        return s;
    }
};
#endif

/** Reads multitouch evdev events from direct touch screens on Linux. */
class LinuxTouchReader
{
public:
    static constexpr size_t kBufferSize = sizeof(struct input_event) * 64;

    explicit LinuxTouchReader (ITouchSyscalls& syscalls, int rotation = 90) noexcept
        : syscalls_ (syscalls),
          decoder_ (800, 1280, rotation)
    {
        discover();
    }

#if defined(__linux__)
    explicit LinuxTouchReader (int rotation = 90) noexcept
        : LinuxTouchReader (LinuxTouchSyscalls::instance(), rotation)
    {
    }
#endif

    ~LinuxTouchReader()
    {
        closeNode();
    }

    /** Descriptor of the open evdev node, or -1 when not connected. */
    int fd() const noexcept { return fd_; }

    /** Node index (e.g. 3 for /dev/input/event3), or -1 if not connected. */
    int nodeIndex() const noexcept { return nodeIndex_; }

    /** Underlying TouchDecoder. */
    TouchDecoder& decoder() noexcept { return decoder_; }
    const TouchDecoder& decoder() const noexcept { return decoder_; }

    /** Scans /dev/input/event0..31 for direct touchscreen with ABS_MT_POSITION_X and ABS_MT_POSITION_Y. */
    bool discover()
    {
        if (fd_ >= 0)
            closeNode();

        for (int i = 0; i < 32; ++i)
        {
            char path[64];
            std::snprintf (path, sizeof(path), "/dev/input/event%d", i);

            // NEVER write, open read-only non-blocking cloexec
            int fd = syscalls_.open (path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd < 0)
                continue;

            // 1. Check EV_ABS capability bit
            uint8_t evBits[4] = {0};
            if (syscalls_.ioctl (fd, EVIOCGBIT (0, sizeof(evBits)), evBits) < 0)
            {
                syscalls_.close (fd);
                continue;
            }
            if (! (evBits[EV_ABS / 8] & (1u << (EV_ABS % 8))))
            {
                syscalls_.close (fd);
                continue;
            }

            // 2. Check INPUT_PROP_DIRECT property bit
            uint8_t propBits[4] = {0};
            if (syscalls_.ioctl (fd, EVIOCGPROP (sizeof(propBits)), propBits) < 0)
            {
                syscalls_.close (fd);
                continue;
            }
            if (! (propBits[INPUT_PROP_DIRECT / 8] & (1u << (INPUT_PROP_DIRECT % 8))))
            {
                syscalls_.close (fd);
                continue;
            }

            // 3. Check ABS_MT_POSITION_X and ABS_MT_POSITION_Y bits
            uint8_t absBits[16] = {0};
            if (syscalls_.ioctl (fd, EVIOCGBIT (EV_ABS, sizeof(absBits)), absBits) < 0)
            {
                syscalls_.close (fd);
                continue;
            }
            const bool hasMtX = (absBits[ABS_MT_POSITION_X / 8] & (1u << (ABS_MT_POSITION_X % 8))) != 0;
            const bool hasMtY = (absBits[ABS_MT_POSITION_Y / 8] & (1u << (ABS_MT_POSITION_Y % 8))) != 0;
            if (! hasMtX || ! hasMtY)
            {
                syscalls_.close (fd);
                continue;
            }

            // 4. Read ABS ranges
            struct input_absinfo absX = {};
            struct input_absinfo absY = {};
            if (syscalls_.ioctl (fd, EVIOCGABS (ABS_MT_POSITION_X), &absX) < 0 ||
                syscalls_.ioctl (fd, EVIOCGABS (ABS_MT_POSITION_Y), &absY) < 0)
            {
                syscalls_.close (fd);
                continue;
            }

            // Valid touch screen found!
            fd_ = fd;
            nodeIndex_ = i;
            buffered_ = 0;
            decoder_.setRanges (absX.minimum, absX.maximum, absY.minimum, absY.maximum);
            return true;
        }

        lastFailMs_ = syscalls_.nowMs();
        return false;
    }

    /** Polls the device, reads all pending input_event records and feeds the decoder.
        Returns number of input_events successfully decoded, or < 0 on fatal read/poll error. */
    int poll (int timeoutMs = 0)
    {
        return pollImpl (timeoutMs, [this] (uint16_t type, uint16_t code, int32_t value, uint64_t timeUs) { decoder_.feed (type, code, value, timeUs); });
    }

    /** Like poll, and hands every touch event to handler (const TouchEvent&) AS SOON AS it is decoded (the decoder's queue is not used: a long gap between polls cannot overflow it). */
    template <typename Handler>
    int poll (int timeoutMs, Handler&& handler)
    {
        const int ret = pollImpl (timeoutMs, [this, &handler] (uint16_t type, uint16_t code, int32_t value, uint64_t timeUs) { decoder_.feed (type, code, value, timeUs, handler); });
        TouchEvent ev;
        while (decoder_.pollEvent (ev))   // what the decoder queued itself: the Up of a finger that was down when the device went away
            handler (ev);
        return ret;
    }

    void closeNode() noexcept
    {
        if (fd_ >= 0)
        {
            syscalls_.close (fd_);
            fd_ = -1;
        }
        nodeIndex_ = -1;
        buffered_ = 0;
        decoder_.reset (syscalls_.nowMs() * 1000ULL);
    }

private:
    template <typename FeedFn>
    int pollImpl (int timeoutMs, FeedFn&& feedFn)
    {
        const uint64_t now = syscalls_.nowMs();

        // If not connected, check back-off of at least 1 second (1000 ms) before rediscovering
        if (fd_ < 0)
        {
            if (lastFailMs_ != 0 && now < lastFailMs_ + 1000)
                return 0;

            if (! discover())
                return 0;
        }

        if (timeoutMs != 0 && fd_ >= 0)
        {
            struct pollfd pfd;
            pfd.fd = fd_;
            pfd.events = POLLIN;
            pfd.revents = 0;
            const int pr = syscalls_.poll (&pfd, 1, timeoutMs);
            if (pr < 0)
            {
                if (errno == EINTR)
                    return 0;
                closeNode();
                lastFailMs_ = syscalls_.nowMs();
                return -1;
            }
            if (pr == 0)
                return 0; // Timeout
            if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
            {
                closeNode();
                lastFailMs_ = syscalls_.nowMs();
                return -1;
            }
        }

        size_t eventsProcessed = 0;

        while (fd_ >= 0)
        {
            const ssize_t n = syscalls_.read (fd_, buffer_ + buffered_, sizeof(buffer_) - buffered_);
            if (n > 0)
            {
                buffered_ += static_cast<size_t> (n);
                size_t offset = 0;
                while (offset + sizeof(struct input_event) <= buffered_)
                {
                    struct input_event ev;
                    std::memcpy (&ev, buffer_ + offset, sizeof(struct input_event));
                    offset += sizeof(struct input_event);

                    const uint64_t timeUs = static_cast<uint64_t> (ev.time.tv_sec) * 1000000ULL +
                                            static_cast<uint64_t> (ev.time.tv_usec);
                    feedFn (ev.type, ev.code, ev.value, timeUs);
                    ++eventsProcessed;
                }

                if (offset > 0)
                {
                    if (offset < buffered_)
                        std::memmove (buffer_, buffer_ + offset, buffered_ - offset);
                    buffered_ -= offset;
                }
            }
            else if (n < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    // EAGAIN is normal: drained all pending events
                    break;
                }
                if (errno == EINTR)
                {
                    continue;
                }

                // ENODEV or read error: close and back off
                closeNode();
                lastFailMs_ = syscalls_.nowMs();
                break;
            }
            else // n == 0 (EOF, device unplugged/removed)
            {
                closeNode();
                lastFailMs_ = syscalls_.nowMs();
                break;
            }
        }

        return static_cast<int> (eventsProcessed);
    }

    ITouchSyscalls& syscalls_;
    TouchDecoder decoder_;
    int fd_ = -1;
    int nodeIndex_ = -1;
    uint64_t lastFailMs_ = 0;
    size_t buffered_ = 0;
    uint8_t buffer_[kBufferSize] = {};
};

using TouchReader = LinuxTouchReader;

/** The touch screen for the controller: the evdev reader (it finds the node itself, opens it read only and never grabs it), told the panel size and the rotation of the overlay's discovery every step. */
class TouchInput final : public ITouchInput
{
public:
    explicit TouchInput (ITouchSyscalls& sys) : reader_ (sys, 90) {}
#if defined(__linux__)
    TouchInput() : reader_ (90) {}
#endif
    void configure (int panelW, int panelH, int rotation) override
    {
        TouchDecoder& d = reader_.decoder();
        if (d.panelW() != panelW || d.panelH() != panelH)
            d.setPanelSize (panelW, panelH);
        if (d.rotation() != TouchDecoder::normaliseRotation (rotation))
            d.setRotation (rotation);
    }
    int poll (const std::function<void (const TouchEvent&)>& handler) override { return reader_.poll (0, handler); }
    std::string describe() const override
    {
        const TouchDecoder& d = reader_.decoder();
        char b[256];
        std::snprintf (b, sizeof b, "touch_node: %s%d\ntouch_abs_range: x %d..%d y %d..%d\ntouch_panel: %dx%d\ntouch_rotation: %d\n", reader_.nodeIndex() >= 0 ? "/dev/input/event" : "none ", reader_.nodeIndex() >= 0 ? reader_.nodeIndex() : 0,
                       d.minX(), d.maxX(), d.minY(), d.maxY(), d.panelW(), d.panelH(), d.rotation());
        return b;
    }
    LinuxTouchReader& reader() noexcept { return reader_; }

private:
    LinuxTouchReader reader_;
};

}} // namespace synthyboy_mpc::overlay
