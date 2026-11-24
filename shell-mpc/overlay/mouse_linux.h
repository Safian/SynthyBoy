// SynthyBoy - MPC overlay: the evdev reader of the mice (EVIOCGBIT). Every node /dev/input/event0 .. 31 that has relative X and Y axes and the left button is a mouse; several at once; a mouse that
// is plugged in while the plugin runs is picked up at the next scan (every 2 s while the plugin-drawn pages are on screen), one that is unplugged is dropped (a button that was down ends with an Up).
// The nodes are opened read only and non blocking and are NEVER grabbed (no EVIOCGRAB): the host keeps getting every event. Same syscall seam as the touch reader, so the tests have fakes.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "mouse_input.h"
#include "touch_linux.h"

namespace synthyboy_mpc { namespace overlay {

class LinuxMouseReader final : public IMouseInput
{
public:
    static constexpr int kMaxDevices = 8;
    static constexpr uint64_t kRescanMs = 2000;
    static constexpr uint16_t kEvKeyBit = 1, kEvRelBit = 2;

    explicit LinuxMouseReader (ITouchSyscalls& sys) : sys_ (sys) {}
#if defined(__linux__)
    LinuxMouseReader() : sys_ (LinuxTouchSyscalls::instance()) {}
#endif
    ~LinuxMouseReader() override { for (Dev& d : devs_) sys_.close (d.fd); }

    int deviceCount() const noexcept { return static_cast<int> (devs_.size()); }
    unsigned long scans() const noexcept { return scans_; }
    unsigned long eventsRead() const noexcept { return events_; }

    void poll (const std::function<void (const MouseEvent&)>& handler) override
    {
        const uint64_t now = sys_.nowMs();
        if (! scanned_ || now >= lastScanMs_ + kRescanMs)
        {
            scanned_ = true;
            lastScanMs_ = now;
            scan();
        }
        readAll (&handler);
    }

    void drain() override { readAll (nullptr); }

    std::string describe() const override
    {
        std::string nodes;
        for (const Dev& d : devs_)
            nodes += (nodes.empty() ? "" : " ") + std::string ("event") + std::to_string (d.node);
        char b[160];
        std::snprintf (b, sizeof b, "mouse_devices: %d\nmouse_nodes: %s\nmouse_scans: %lu\nmouse_hotplugs: %lu added, %lu dropped\n", deviceCount(), nodes.empty() ? "none" : nodes.c_str(), scans_, added_, dropped_);
        return b;
    }

private:
    struct Dev
    {
        int fd = -1, node = -1, id = 0;
        MouseDecoder dec;
        uint8_t buf[sizeof (struct input_event) * 64] = {};
        size_t buffered = 0;
    };

    bool isMouse (int fd)
    {
        uint8_t ev[4] = {};
        if (sys_.ioctl (fd, EVIOCGBIT (0, sizeof ev), ev) < 0)
            return false;
        if (! (ev[kEvRelBit / 8] & (1u << (kEvRelBit % 8))) || ! (ev[kEvKeyBit / 8] & (1u << (kEvKeyBit % 8))))
            return false;
        uint8_t rel[4] = {};
        if (sys_.ioctl (fd, EVIOCGBIT (kEvRelBit, sizeof rel), rel) < 0)
            return false;
        auto has = [] (const uint8_t* bits, int n) { return (bits[n / 8] & (1u << (n % 8))) != 0; };
        if (! has (rel, MouseDecoder::kRelX) || ! has (rel, MouseDecoder::kRelY))
            return false;
        uint8_t key[96] = {};   // KEY_MAX 0x2ff
        if (sys_.ioctl (fd, EVIOCGBIT (kEvKeyBit, sizeof key), key) < 0)
            return false;
        return has (key, MouseDecoder::kBtnLeft);
    }

    void scan()
    {
        ++scans_;
        for (int i = 0; i < 32 && static_cast<int> (devs_.size()) < kMaxDevices; ++i)
        {
            bool open = false;
            for (const Dev& d : devs_)
                open = open || d.node == i;
            if (open)
                continue;
            char path[64];
            std::snprintf (path, sizeof path, "/dev/input/event%d", i);
            uint64_t stamp = 0;
            const bool stamped = sys_.nodeStamp (path, stamp);
            if (stamped && notMouse_[i] == stamp)
                continue;                  // known not to be a mouse and unchanged (one stat instead of an open and three ioctls every two seconds)
            const int fd = sys_.open (path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);   // read only, never grabbed
            if (fd < 0)
                continue;
            if (! isMouse (fd))
            {
                sys_.close (fd);
                if (stamped)
                    notMouse_[i] = stamp;
                continue;
            }
            Dev d;
            d.fd = fd;
            d.node = i;
            d.id = nextId_++;
            devs_.push_back (d);
            ++added_;
        }
    }

    void readAll (const std::function<void (const MouseEvent&)>* handler)
    {
        for (size_t k = 0; k < devs_.size();)
        {
            Dev& d = devs_[k];
            bool gone = false;
            for (;;)
            {
                const ssize_t n = sys_.read (d.fd, d.buf + d.buffered, sizeof d.buf - d.buffered);
                if (n > 0)
                {
                    d.buffered += static_cast<size_t> (n);
                    size_t off = 0;
                    while (off + sizeof (struct input_event) <= d.buffered)
                    {
                        struct input_event ev;
                        std::memcpy (&ev, d.buf + off, sizeof ev);
                        off += sizeof ev;
                        ++events_;
                        const uint64_t t = static_cast<uint64_t> (ev.time.tv_sec) * 1000000ULL + static_cast<uint64_t> (ev.time.tv_usec);
                        if (handler != nullptr)
                            d.dec.feed (ev.type, ev.code, ev.value, t, d.id, *handler);
                        else
                            d.dec.feed (ev.type, ev.code, ev.value, t, d.id, [] (const MouseEvent&) {});
                    }
                    if (off > 0 && off < d.buffered)
                        std::memmove (d.buf, d.buf + off, d.buffered - off);
                    d.buffered -= off;
                    continue;
                }
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                    break;
                if (n < 0 && errno == EINTR)
                    continue;
                gone = true;     // ENODEV, a read error, or end of file: unplugged
                break;
            }
            if (! gone)
            {
                ++k;
                continue;
            }
            if (d.dec.buttonDown() && handler != nullptr)
            {
                MouseEvent up;
                up.kind = MouseEvent::Up;
                up.device = d.id;
                (*handler) (up);   // a button that was down when the mouse went: the gesture ends
            }
            sys_.close (d.fd);
            ++dropped_;
            devs_.erase (devs_.begin() + static_cast<std::ptrdiff_t> (k));
        }
    }

    ITouchSyscalls& sys_;
    std::vector<Dev> devs_;
    uint64_t lastScanMs_ = 0;
    uint64_t notMouse_[32] = {};   // the stamp of every node that was looked at and is no mouse
    bool scanned_ = false;
    int nextId_ = 0;
    unsigned long scans_ = 0, events_ = 0, added_ = 0, dropped_ = 0;
};

}} // namespace synthyboy_mpc::overlay
