// SynthyBoy - MPC overlay tests: TouchDecoder multitouch B decoding, coordinate mapping,
// rotation, edge case recovery, and LinuxTouchReader evdev scanning and polling with fakes.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../geometry.h"
#include "../touch_decoder.h"
#include "../touch_linux.h"

using namespace synthyboy_mpc::overlay;

static int g_fails = 0;

static void check (bool ok, const std::string& what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok)
        ++g_fails;
}

// -------------------------------------------------------------------------------------------------
// Decoder Helper Functions
// -------------------------------------------------------------------------------------------------

static void sendSlot (TouchDecoder& dec, int slot, uint64_t timeUs = 0)
{
    dec.feed (TouchDecoder::kEvAbs, TouchDecoder::kAbsMtSlot, slot, timeUs);
}

static void sendTrackingId (TouchDecoder& dec, int trackingId, uint64_t timeUs = 0)
{
    dec.feed (TouchDecoder::kEvAbs, TouchDecoder::kAbsMtTrackingId, trackingId, timeUs);
}

static void sendCoords (TouchDecoder& dec, int x, int y, uint64_t timeUs = 0)
{
    dec.feed (TouchDecoder::kEvAbs, TouchDecoder::kAbsMtPositionX, x, timeUs);
    dec.feed (TouchDecoder::kEvAbs, TouchDecoder::kAbsMtPositionY, y, timeUs);
}

static void sendSyn (TouchDecoder& dec, uint64_t timeUs = 0)
{
    dec.feed (TouchDecoder::kEvSyn, TouchDecoder::kSynReport, 0, timeUs);
}

static void sendSynDropped (TouchDecoder& dec, uint64_t timeUs = 0)
{
    dec.feed (TouchDecoder::kEvSyn, TouchDecoder::kSynDropped, 0, timeUs);
}

static void sendTapDown (TouchDecoder& dec, int slot, int trackingId, int x, int y, uint64_t timeUs = 0)
{
    sendSlot (dec, slot, timeUs);
    sendTrackingId (dec, trackingId, timeUs);
    sendCoords (dec, x, y, timeUs);
    sendSyn (dec, timeUs);
}

static void sendMove (TouchDecoder& dec, int slot, int x, int y, uint64_t timeUs = 0)
{
    sendSlot (dec, slot, timeUs);
    sendCoords (dec, x, y, timeUs);
    sendSyn (dec, timeUs);
}

static void sendLift (TouchDecoder& dec, int slot, uint64_t timeUs = 0)
{
    sendSlot (dec, slot, timeUs);
    sendTrackingId (dec, -1, timeUs);
    sendSyn (dec, timeUs);
}

static std::vector<TouchEvent> collectEvents (TouchDecoder& dec)
{
    std::vector<TouchEvent> out;
    TouchEvent ev;
    while (dec.pollEvent (ev))
        out.push_back (ev);
    return out;
}

// -------------------------------------------------------------------------------------------------
// Tests: TouchDecoder
// -------------------------------------------------------------------------------------------------

static void testTap()
{
    // MPC Key 61 panel: 800 x 1280, rotation 90 -> UI: 1280 x 800.
    // Panel point (sx=100, sy=200):
    // Rotation 90: UI X = 1280 - 1 - sy = 1079; UI Y = sx = 100.
    TouchDecoder dec (800, 1280, 90);

    sendTapDown (dec, 0, 10, 100, 200, 1000);
    auto events = collectEvents (dec);

    check (events.size() == 1, "tap: exactly one Down event emitted");
    if (! events.empty())
    {
        check (events[0].kind == TouchEvent::Down, "tap: event kind is Down");
        check (events[0].x == 1079 && events[0].y == 100, "tap: mapped UI coords (1079, 100)");
        check (events[0].timeUs == 1000, "tap: timeUs matches");
    }
    check (dec.isDown(), "tap: decoder reports finger is down");
    check (dec.trackedSlot() == 0, "tap: tracked slot is 0");

    sendLift (dec, 0, 2000);
    events = collectEvents (dec);

    check (events.size() == 1, "tap: exactly one Up event emitted");
    if (! events.empty())
    {
        check (events[0].kind == TouchEvent::Up, "tap: event kind is Up");
        check (events[0].x == 1079 && events[0].y == 100, "tap: Up at last known coords");
        check (events[0].timeUs == 2000, "tap: Up timeUs matches");
    }
    check (! dec.isDown(), "tap: finger no longer down");
    check (dec.trackedSlot() == -1, "tap: tracked slot reset to -1");
}

static void testDrag()
{
    TouchDecoder dec (800, 1280, 90);

    // Down at (100, 200) -> UI (1079, 100)
    sendTapDown (dec, 0, 1, 100, 200, 1000);
    // Move to (105, 210) -> UI (1279 - 210 = 1069, 105)
    sendMove (dec, 0, 105, 210, 1020);
    // Move to (110, 220) -> UI (1279 - 220 = 1059, 110)
    sendMove (dec, 0, 110, 220, 1040);
    // Send same coordinates again (no change in UI) -> should not emit Move
    sendMove (dec, 0, 110, 220, 1060);
    // Lift
    sendLift (dec, 0, 1080);

    auto events = collectEvents (dec);
    check (events.size() == 4, "drag: exactly 4 events (Down, Move, Move, Up)");
    if (events.size() == 4)
    {
        check (events[0].kind == TouchEvent::Down && events[0].x == 1079 && events[0].y == 100, "drag: Down correct");
        check (events[1].kind == TouchEvent::Move && events[1].x == 1069 && events[1].y == 105, "drag: Move 1 correct");
        check (events[2].kind == TouchEvent::Move && events[2].x == 1059 && events[2].y == 110, "drag: Move 2 correct");
        check (events[3].kind == TouchEvent::Up   && events[3].x == 1059 && events[3].y == 110, "drag: Up correct");
    }
}

static void testTwoFingers()
{
    // Prompt requirement: "two fingers (second ignored; first lifts while second stays; then a new first)"
    TouchDecoder dec (800, 1280, 90);

    // 1. First finger touches down in slot 0
    sendTapDown (dec, 0, 1, 100, 200, 1000);
    auto e1 = collectEvents (dec);
    check (e1.size() == 1 && e1[0].kind == TouchEvent::Down, "two-finger: finger 1 emitted Down");
    check (dec.trackedSlot() == 0, "two-finger: tracked slot is 0");

    // 2. Second finger touches down in slot 1 while slot 0 is held
    sendTapDown (dec, 1, 2, 300, 400, 1050);
    auto e2 = collectEvents (dec);
    check (e2.empty(), "two-finger: second finger down is ignored (0 events)");
    check (dec.trackedSlot() == 0, "two-finger: tracked slot remains 0");

    // 3. Second finger moves: must be ignored
    sendMove (dec, 1, 310, 410, 1100);
    auto e3 = collectEvents (dec);
    check (e3.empty(), "two-finger: second finger move is ignored (0 events)");

    // 4. First finger moves: must emit Move
    sendMove (dec, 0, 105, 205, 1150);
    auto e4 = collectEvents (dec);
    check (e4.size() == 1 && e4[0].kind == TouchEvent::Move, "two-finger: first finger move emitted Move");

    // 5. First finger lifts while second finger stays down
    sendLift (dec, 0, 1200);
    auto e5 = collectEvents (dec);
    check (e5.size() == 1 && e5[0].kind == TouchEvent::Up, "two-finger: first finger lift emitted Up");
    check (! dec.isDown(), "two-finger: no finger tracked after first lifted");
    check (dec.trackedSlot() == -1, "two-finger: tracked slot reset to -1");

    // 6. Second finger moves while still held: must stay ignored
    sendMove (dec, 1, 320, 420, 1250);
    auto e6 = collectEvents (dec);
    check (e6.empty(), "two-finger: remaining second finger move stays ignored (0 events)");

    // 7. A new first finger touches down in slot 0 (or any slot)
    sendTapDown (dec, 0, 3, 500, 600, 1300);
    auto e7 = collectEvents (dec);
    check (e7.size() == 1 && e7[0].kind == TouchEvent::Down, "two-finger: new first finger emitted Down");
    check (dec.trackedSlot() == 0, "two-finger: tracked slot is new finger (slot 0)");

    // 8. New first finger lifts
    sendLift (dec, 0, 1350);
    auto e8 = collectEvents (dec);
    check (e8.size() == 1 && e8[0].kind == TouchEvent::Up, "two-finger: new first finger lift emitted Up");

    // 9. Second finger finally lifts: ignored
    sendLift (dec, 1, 1400);
    auto e9 = collectEvents (dec);
    check (e9.empty(), "two-finger: second finger lift is ignored (0 events)");
}

static void testAllRotationsWithCornerPoints()
{
    // Panel 800 x 1280.
    // Corners: (0, 0), (799, 0), (0, 1279), (799, 1279).
    const int W = 800;
    const int H = 1280;

    struct CornerTest
    {
        int rotation;
        int sx, sy;
        int expectedUiX, expectedUiY;
    };

    const CornerTest tests[] = {
        // Rotation 0: (sx, sy)
        { 0, 0, 0, 0, 0 },
        { 0, 799, 0, 799, 0 },
        { 0, 0, 1279, 0, 1279 },
        { 0, 799, 1279, 799, 1279 },
        { 0, 400, 640, 400, 640 },

        // Rotation 90: (H - 1 - sy, sx)
        { 90, 0, 0, 1279, 0 },
        { 90, 799, 0, 1279, 799 },
        { 90, 0, 1279, 0, 0 },
        { 90, 799, 1279, 0, 799 },
        { 90, 400, 640, 639, 400 },

        // Rotation 180: (W - 1 - sx, H - 1 - sy)
        { 180, 0, 0, 799, 1279 },
        { 180, 799, 0, 0, 1279 },
        { 180, 0, 1279, 799, 0 },
        { 180, 799, 1279, 0, 0 },
        { 180, 400, 640, 399, 639 },

        // Rotation 270: (sy, W - 1 - sx)
        { 270, 0, 0, 0, 799 },
        { 270, 799, 0, 0, 0 },
        { 270, 0, 1279, 1279, 799 },
        { 270, 799, 1279, 1279, 0 },
        { 270, 400, 640, 640, 399 },
    };

    for (const auto& t : tests)
    {
        TouchDecoder dec (W, H, t.rotation);
        int uiX = -1, uiY = -1;
        dec.mapPanelToUi (t.sx, t.sy, uiX, uiY);

        char msg[128];
        std::snprintf (msg, sizeof(msg), "rot %d: panel (%d, %d) -> UI (%d, %d)",
                       t.rotation, t.sx, t.sy, uiX, uiY);
        check (uiX == t.expectedUiX && uiY == t.expectedUiY, msg);
    }
}

static void testSynDropped()
{
    // Prompt requirement: "Handles SYN_DROPPED (code 3: state is lost; emit Up if a finger was tracked and resynchronise from the next complete frame)"
    TouchDecoder dec (800, 1280, 90);

    // Finger down
    sendTapDown (dec, 0, 10, 100, 200, 1000);
    auto e1 = collectEvents (dec);
    check (e1.size() == 1 && e1[0].kind == TouchEvent::Down, "syn_dropped: Down event emitted");
    check (dec.isDown(), "syn_dropped: finger down before drop");

    // SYN_DROPPED arrives
    sendSynDropped (dec, 1500);
    auto e2 = collectEvents (dec);
    check (e2.size() == 1 && e2[0].kind == TouchEvent::Up, "syn_dropped: emits Up on SYN_DROPPED");
    if (! e2.empty())
    {
        check (e2[0].timeUs == 1500, "syn_dropped: Up event has timestamp of drop");
        check (e2[0].x == 1079 && e2[0].y == 100, "syn_dropped: Up event at last known coords");
    }
    check (! dec.isDown(), "syn_dropped: finger no longer down after drop");
    check (dec.trackedSlot() == -1, "syn_dropped: tracked slot reset to -1");

    // the rest of the dropped packet (a half frame: a new finger's id and position) is discarded up to its SYN_REPORT
    sendSlot (dec, 0, 1600);
    sendTrackingId (dec, 10, 1600);
    sendCoords (dec, 50, 60, 1600);
    sendSyn (dec, 1600);
    check (collectEvents (dec).empty() && ! dec.isDown(), "syn_dropped: the events of the dropped packet are discarded");

    // Next complete frame arrives
    sendTapDown (dec, 0, 11, 200, 300, 2000);
    auto e3 = collectEvents (dec);
    check (e3.size() == 1 && e3[0].kind == TouchEvent::Down, "syn_dropped: next complete frame emits Down");
    check (dec.isDown(), "syn_dropped: resynchronised finger is down");

    sendLift (dec, 0, 2500);
    auto e4 = collectEvents (dec);
    check (e4.size() == 1 && e4[0].kind == TouchEvent::Up, "syn_dropped: clean Up after resync");

    // SYN_DROPPED when no finger was tracked -> no events emitted
    sendSynDropped (dec, 3000);
    auto e5 = collectEvents (dec);
    check (e5.empty(), "syn_dropped: no event when no finger was tracked");
}

static void testScaledRanges()
{
    // The Key 61's panel reports 0 .. 2048 on both axes for an 800 x 1280 panel: the ABS range is not the pixel range
    TouchDecoder dec (800, 1280, 90);
    dec.setRanges (0, 2048, 0, 2048);
    int x = -1, y = -1;
    dec.mapPanelToUi (0, 0, x, y);
    check (x == 1279 && y == 0, "scaled range: top left of the device -> UI (1279, 0) under rotation 90");
    dec.mapPanelToUi (2048, 2048, x, y);
    check (x == 0 && y == 799, "scaled range: bottom right of the device -> UI (0, 799)");
    dec.mapPanelToUi (1024, 1024, x, y);
    check (x >= 639 && x <= 640 && y >= 399 && y <= 400, "scaled range: the middle is the middle");
    // what mtouch writes for the upright pixel (X, Y): sx = Y, sy = 1279 - X, scaled by range / 799 and / 1279 -> back within one pixel
    for (int X : { 0, 1, 100, 640, 1000, 1279 })
        for (int Y : { 0, 3, 400, 799 })
        {
            const int sx = Y, sy = 1279 - X;
            const int px = static_cast<int> (static_cast<long> (sx) * 2048 / 799), py = static_cast<int> (static_cast<long> (sy) * 2048 / 1279);
            dec.mapPanelToUi (px, py, x, y);
            char msg[96];
            std::snprintf (msg, sizeof msg, "scaled range: injected UI (%d, %d) comes back as (%d, %d)", X, Y, x, y);
            check (std::abs (x - X) <= 1 && std::abs (y - Y) <= 1, msg);
        }
}

static void testGeometryAgreement()
{
    // the picture is placed by geometry.h; a touch must land on the pixel that picture shows: mapping a panel pixel back must give the UI pixel that uiPointToPanel put there
    for (int rot : { 0, 90, 180, 270 })
    {
        const bool swap = rot == 90 || rot == 270;
        const int uiW = swap ? 1280 : 800, uiH = swap ? 800 : 1280;
        TouchDecoder dec (800, 1280, rot);
        bool all = true;
        for (int X = 0; X < uiW; X += 37)
            for (int Y = 0; Y < uiH; Y += 29)
            {
                int px = 0, py = 0, bx = -1, by = -1;
                synthyboy_mpc::overlay::uiPointToPanel (rot, X, Y, 800, 1280, px, py);
                dec.mapPanelToUi (px, py, bx, by);
                all = all && bx == X && by == Y;
            }
        check (all, "geometry.h agreement: panel pixel -> UI pixel inverts uiPointToPanel, rotation " + std::to_string (rot));
    }
}

static void testOneFrameTap()
{
    TouchDecoder dec (800, 1280, 90);
    sendSlot (dec, 0, 10);
    sendTrackingId (dec, 7, 10);
    sendCoords (dec, 100, 100, 10);
    sendTrackingId (dec, -1, 10);
    sendSyn (dec, 10);
    check (collectEvents (dec).empty() && ! dec.isDown(), "a finger that came and went within one frame reports nothing (no Up without a Down)");
    sendTapDown (dec, 0, 8, 100, 100, 20);
    check (collectEvents (dec).size() == 1 && dec.isDown(), "the next finger is reported normally");
}

static void testHandlerNeedsNoQueue()
{
    // 40 events of a drag handed to the sink: nothing is queued, nothing is lost beyond the queue's 16
    TouchDecoder dec (800, 1280, 90);
    int n = 0;
    auto sink = [&n] (const TouchEvent&) { ++n; };
    dec.feed (TouchDecoder::kEvAbs, TouchDecoder::kAbsMtTrackingId, 1, 0, sink);
    dec.feed (TouchDecoder::kEvAbs, TouchDecoder::kAbsMtPositionX, 10, 0, sink);
    dec.feed (TouchDecoder::kEvAbs, TouchDecoder::kAbsMtPositionY, 10, 0, sink);
    dec.feed (TouchDecoder::kEvSyn, TouchDecoder::kSynReport, 0, 0, sink);
    for (int i = 0; i < 40; ++i)
    {
        dec.feed (TouchDecoder::kEvAbs, TouchDecoder::kAbsMtPositionX, 20 + i, 0, sink);
        dec.feed (TouchDecoder::kEvSyn, TouchDecoder::kSynReport, 0, 0, sink);
    }
    check (n == 41 && dec.available() == 0, "sink version: 1 Down + 40 Moves delivered, the queue stays empty");
}

static void testTrackingIdReplaced()
{
    // Prompt requirement: "a tracking id change without an Up" / "tracking id replaced"
    TouchDecoder dec (800, 1280, 90);

    // Initial finger down with tracking id 10 at (100, 200) -> UI (1079, 100)
    sendTapDown (dec, 0, 10, 100, 200, 1000);
    auto e1 = collectEvents (dec);
    check (e1.size() == 1 && e1[0].kind == TouchEvent::Down, "replaced: initial Down");

    // Without a preceding Up (-1), slot 0 receives a new tracking ID 20 at (150, 250)
    // UI X = 1279 - 250 = 1029, UI Y = 150
    sendSlot (dec, 0, 2000);
    sendTrackingId (dec, 20, 2000);
    sendCoords (dec, 150, 250, 2000);
    sendSyn (dec, 2000);

    auto e2 = collectEvents (dec);
    check (e2.size() == 2, "replaced: emits exactly 2 events (Up for old, Down for new)");
    if (e2.size() == 2)
    {
        check (e2[0].kind == TouchEvent::Up, "replaced: first event is Up");
        check (e2[0].x == 1079 && e2[0].y == 100, "replaced: Up coords match previous touch");
        check (e2[0].timeUs == 2000, "replaced: Up time matches");

        check (e2[1].kind == TouchEvent::Down, "replaced: second event is Down");
        check (e2[1].x == 1029 && e2[1].y == 150, "replaced: Down coords match new touch");
        check (e2[1].timeUs == 2000, "replaced: Down time matches");
    }
    check (dec.isDown(), "replaced: finger still down with new tracking id");
    check (dec.trackedTrackingId() == 20, "replaced: new tracking id is 20");

    // Clean lift of new tracking id
    sendLift (dec, 0, 3000);
    auto e3 = collectEvents (dec);
    check (e3.size() == 1 && e3[0].kind == TouchEvent::Up, "replaced: subsequent lift emits Up");
    if (! e3.empty())
    {
        check (e3[0].x == 1029 && e3[0].y == 150, "replaced: lift coords match new touch");
    }
}

static void testOutOfRangeClamp()
{
    // Prompt requirement: "coordinates outside the range (clamped)"
    TouchDecoder dec (800, 1280, 0); // Rotation 0: (sx, sy)

    // Negative coordinates -> clamp to (0, 0)
    sendTapDown (dec, 0, 1, -100, -200, 1000);
    auto e1 = collectEvents (dec);
    check (e1.size() == 1, "clamp: Down emitted");
    if (! e1.empty())
    {
        check (e1[0].x == 0 && e1[0].y == 0, "clamp: negative coords clamped to (0, 0)");
    }

    // Coordinates beyond panel dimensions -> clamp to (799, 1279)
    sendMove (dec, 0, 2000, 3000, 1050);
    auto e2 = collectEvents (dec);
    check (e2.size() == 1, "clamp: Move emitted");
    if (! e2.empty())
    {
        check (e2[0].x == 799 && e2[0].y == 1279, "clamp: overflow coords clamped to (799, 1279)");
    }

    sendLift (dec, 0, 1100);
}

static void testTimeStamps()
{
    // Prompt requirement: "time in microseconds"
    TouchDecoder dec (800, 1280, 90);

    const uint64_t t0 = 123456789ULL;
    const uint64_t t1 = 123456888ULL;
    const uint64_t t2 = 123456999ULL;

    sendTapDown (dec, 0, 1, 100, 200, t0);
    sendMove (dec, 0, 105, 205, t1);
    sendLift (dec, 0, t2);

    auto events = collectEvents (dec);
    check (events.size() == 3, "timestamps: 3 events");
    if (events.size() == 3)
    {
        check (events[0].timeUs == t0, "timestamps: Down timeUs matches");
        check (events[1].timeUs == t1, "timestamps: Move timeUs matches");
        check (events[2].timeUs == t2, "timestamps: Up timeUs matches");
    }
}

// -------------------------------------------------------------------------------------------------
// Tests: LinuxTouchReader with Fake Syscalls
// -------------------------------------------------------------------------------------------------

class FakeTouchSyscalls final : public ITouchSyscalls
{
public:
    struct Node
    {
        bool exists = false;
        bool hasEvAbs = false;
        bool hasPropDirect = false;
        bool hasMtX = false;
        bool hasMtY = false;
        int minX = 0, maxX = 799;
        int minY = 0, maxY = 1279;
        std::vector<uint8_t> stream;
        size_t streamOffset = 0;
        int injectReadErrno = 0;
    };

    Node nodes[32] = {};
    int openFlagsSeen[32] = {};
    int openCount[32] = {};
    int closeCount[32] = {};
    uint64_t clockMs = 1000;
    int writesSeen = 0;
    int grabIoctlsSeen = 0;

    int open (const char* path, int flags) override
    {
        int idx = -1;
        if (std::sscanf (path, "/dev/input/event%d", &idx) == 1 && idx >= 0 && idx < 32)
        {
            if (! nodes[idx].exists)
            {
                errno = ENOENT;
                return -1;
            }
            openFlagsSeen[idx] = flags;
            ++openCount[idx];
            return 100 + idx; // Synthetic fd
        }
        errno = ENOENT;
        return -1;
    }

    int close (int fd) override
    {
        const int idx = fd - 100;
        if (idx >= 0 && idx < 32)
        {
            ++closeCount[idx];
            return 0;
        }
        errno = EBADF;
        return -1;
    }

    ssize_t read (int fd, void* buf, size_t count) override
    {
        const int idx = fd - 100;
        if (idx < 0 || idx >= 32 || ! nodes[idx].exists)
        {
            errno = EBADF;
            return -1;
        }

        Node& n = nodes[idx];
        if (n.injectReadErrno != 0)
        {
            errno = n.injectReadErrno;
            return -1;
        }

        if (n.streamOffset >= n.stream.size())
        {
            errno = EAGAIN;
            return -1;
        }

        const size_t avail = n.stream.size() - n.streamOffset;
        const size_t toCopy = std::min (count, avail);
        std::memcpy (buf, n.stream.data() + n.streamOffset, toCopy);
        n.streamOffset += toCopy;
        return static_cast<ssize_t> (toCopy);
    }

    int ioctl (int fd, unsigned long request, void* arg) override
    {
        const int idx = fd - 100;
        if (idx < 0 || idx >= 32 || ! nodes[idx].exists)
        {
            errno = EBADF;
            return -1;
        }

        Node& n = nodes[idx];

        // Verify NEVER EVIOCGRAB
        if ((request & 0xffff) == 0x4590) // EVIOCGRAB
        {
            ++grabIoctlsSeen;
            errno = EINVAL;
            return -1;
        }

        // Check EVIOCGBIT (0, ...)
        if ((request & ~0x1fff0000u) == (0x80004520u))
        {
            auto* bits = static_cast<uint8_t*> (arg);
            if (n.hasEvAbs)
                bits[EV_ABS / 8] |= static_cast<uint8_t> (1u << (EV_ABS % 8));
            return 0;
        }

        // Check EVIOCGPROP (...)
        if ((request & ~0x1fff0000u) == 0x80004509u)
        {
            auto* bits = static_cast<uint8_t*> (arg);
            if (n.hasPropDirect)
                bits[INPUT_PROP_DIRECT / 8] |= static_cast<uint8_t> (1u << (INPUT_PROP_DIRECT % 8));
            return 0;
        }

        // Check EVIOCGBIT (EV_ABS, ...)
        if ((request & ~0x1fff0000u) == (0x80004520u | EV_ABS))
        {
            auto* bits = static_cast<uint8_t*> (arg);
            if (n.hasMtX)
                bits[ABS_MT_POSITION_X / 8] |= static_cast<uint8_t> (1u << (ABS_MT_POSITION_X % 8));
            if (n.hasMtY)
                bits[ABS_MT_POSITION_Y / 8] |= static_cast<uint8_t> (1u << (ABS_MT_POSITION_Y % 8));
            return 0;
        }

        // Check EVIOCGABS (ABS_MT_POSITION_X)
        if ((request & ~0x1fff0000u) == (0x80004540u | ABS_MT_POSITION_X))
        {
            auto* abs = static_cast<struct input_absinfo*> (arg);
            abs->minimum = n.minX;
            abs->maximum = n.maxX;
            return 0;
        }

        // Check EVIOCGABS (ABS_MT_POSITION_Y)
        if ((request & ~0x1fff0000u) == (0x80004540u | ABS_MT_POSITION_Y))
        {
            auto* abs = static_cast<struct input_absinfo*> (arg);
            abs->minimum = n.minY;
            abs->maximum = n.maxY;
            return 0;
        }

        errno = EINVAL;
        return -1;
    }

    int poll (struct pollfd* fds, nfds_t nfds, int timeoutMs) override
    {
        (void)timeoutMs;
        int ready = 0;
        for (nfds_t i = 0; i < nfds; ++i)
        {
            const int idx = fds[i].fd - 100;
            fds[i].revents = 0;
            if (idx >= 0 && idx < 32 && nodes[idx].exists)
            {
                if (nodes[idx].streamOffset < nodes[idx].stream.size())
                {
                    fds[i].revents |= POLLIN;
                    ++ready;
                }
            }
        }
        return ready;
    }

    uint64_t nowMs() override
    {
        return clockMs;
    }

    void pushInputEvent (int idx, uint16_t type, uint16_t code, int32_t val, uint64_t timeUs)
    {
        struct input_event ev;
        std::memset (&ev, 0, sizeof ev);
        ev.time.tv_sec = static_cast<decltype (ev.time.tv_sec)> (timeUs / 1000000ULL);
        ev.time.tv_usec = static_cast<decltype (ev.time.tv_usec)> (timeUs % 1000000ULL);
        ev.type = type;
        ev.code = code;
        ev.value = val;

        const auto* ptr = reinterpret_cast<const uint8_t*> (&ev);
        nodes[idx].stream.insert (nodes[idx].stream.end(), ptr, ptr + sizeof ev);
    }
};

static void testLinuxReaderDiscovery()
{
    // Prompt requirement: "discovery picks the right node among several"
    FakeTouchSyscalls fake;

    // Node 0: keyboard (no EV_ABS)
    fake.nodes[0].exists = true;

    // Node 1: mouse (EV_ABS without INPUT_PROP_DIRECT)
    fake.nodes[1].exists = true;
    fake.nodes[1].hasEvAbs = true;

    // Node 2: trackpad (EV_ABS and INPUT_PROP_DIRECT, but missing ABS_MT_POSITION_Y)
    fake.nodes[2].exists = true;
    fake.nodes[2].hasEvAbs = true;
    fake.nodes[2].hasPropDirect = true;
    fake.nodes[2].hasMtX = true;

    // Node 3: valid direct touchscreen!
    fake.nodes[3].exists = true;
    fake.nodes[3].hasEvAbs = true;
    fake.nodes[3].hasPropDirect = true;
    fake.nodes[3].hasMtX = true;
    fake.nodes[3].hasMtY = true;
    fake.nodes[3].minX = 0;
    fake.nodes[3].maxX = 799;
    fake.nodes[3].minY = 0;
    fake.nodes[3].maxY = 1279;

    // Node 4: another touchscreen later in list
    fake.nodes[4] = fake.nodes[3];

    LinuxTouchReader reader (fake, 90);

    check (reader.fd() == 103, "discovery: picked event3 (fd == 103)");
    check (reader.nodeIndex() == 3, "discovery: nodeIndex is 3");
    check (reader.decoder().panelW() == 800, "discovery: panelW configured as 800");
    check (reader.decoder().panelH() == 1280, "discovery: panelH configured as 1280");

    // Verify open flags: O_RDONLY | O_NONBLOCK | O_CLOEXEC
    const int expectedFlags = O_RDONLY | O_NONBLOCK | O_CLOEXEC;
    check ((fake.openFlagsSeen[3] & expectedFlags) == expectedFlags, "discovery: opened O_RDONLY | O_NONBLOCK | O_CLOEXEC");
    check (fake.grabIoctlsSeen == 0, "discovery: NEVER EVIOCGRAB");
}

static void testLinuxReaderEagain()
{
    // Prompt requirement: "EAGAIN is normal"
    FakeTouchSyscalls fake;
    fake.nodes[3].exists = true;
    fake.nodes[3].hasEvAbs = true;
    fake.nodes[3].hasPropDirect = true;
    fake.nodes[3].hasMtX = true;
    fake.nodes[3].hasMtY = true;

    LinuxTouchReader reader (fake, 90);
    check (reader.fd() == 103, "eagain: node connected");

    // Poll with empty buffer -> read returns EAGAIN
    const int count = reader.poll (0);
    check (count == 0, "eagain: poll returned 0 events on EAGAIN");
    check (reader.fd() == 103, "eagain: descriptor remains open");
}

static void testLinuxReaderDeviceRemovedAndRefound()
{
    // Prompt requirement: "ENODEV / read error closes and rediscovers with a back-off of at least one second"
    FakeTouchSyscalls fake;
    fake.nodes[3].exists = true;
    fake.nodes[3].hasEvAbs = true;
    fake.nodes[3].hasPropDirect = true;
    fake.nodes[3].hasMtX = true;
    fake.nodes[3].hasMtY = true;
    fake.clockMs = 1000;

    LinuxTouchReader reader (fake, 90);
    check (reader.fd() == 103, "removed: node open at start");

    // Put down a finger so we can test that disconnect emits Up
    fake.pushInputEvent (3, TouchDecoder::kEvAbs, TouchDecoder::kAbsMtSlot, 0, 1000);
    fake.pushInputEvent (3, TouchDecoder::kEvAbs, TouchDecoder::kAbsMtTrackingId, 1, 1000);
    fake.pushInputEvent (3, TouchDecoder::kEvAbs, TouchDecoder::kAbsMtPositionX, 100, 1000);
    fake.pushInputEvent (3, TouchDecoder::kEvAbs, TouchDecoder::kAbsMtPositionY, 200, 1000);
    fake.pushInputEvent (3, TouchDecoder::kEvSyn, TouchDecoder::kSynReport, 0, 1000);

    std::vector<TouchEvent> events;
    reader.poll (0, [&] (const TouchEvent& ev) { events.push_back (ev); });
    check (events.size() == 1 && events[0].kind == TouchEvent::Down, "removed: touch down recorded");
    check (reader.decoder().isDown(), "removed: finger down in decoder");

    // Inject ENODEV (device unplugged / removed)
    fake.nodes[3].injectReadErrno = ENODEV;
    fake.nodes[3].exists = false; // device disappeared from /dev/input
    events.clear();

    reader.poll (0, [&] (const TouchEvent& ev) { events.push_back (ev); });
    check (reader.fd() == -1, "removed: fd closed on ENODEV");
    check (events.size() == 1 && events[0].kind == TouchEvent::Up, "removed: device loss emits Up for held finger");
    check (! reader.decoder().isDown(), "removed: decoder is no longer down");

    // Advance clock by 500 ms (< 1000 ms back-off)
    fake.clockMs += 500;
    const int opensBefore = fake.openCount[3];
    reader.poll (0);
    check (reader.fd() == -1, "removed: remains disconnected during back-off (< 1 sec)");
    check (fake.openCount[3] == opensBefore, "removed: no open syscall attempted during back-off");

    // Device plugged back in; advance clock by another 600 ms (total 1100 ms > 1000 ms back-off)
    fake.nodes[3].exists = true;
    fake.nodes[3].injectReadErrno = 0;
    fake.clockMs += 600;

    reader.poll (0);
    check (reader.fd() == 103, "removed: re-discovered and connected after back-off elapsed");
    check (reader.nodeIndex() == 3, "removed: reconnected to event3");
}

static void testLinuxReaderPartialRecordRead()
{
    // Prompt requirement: "partial record read"
    FakeTouchSyscalls fake;
    fake.nodes[3].exists = true;
    fake.nodes[3].hasEvAbs = true;
    fake.nodes[3].hasPropDirect = true;
    fake.nodes[3].hasMtX = true;
    fake.nodes[3].hasMtY = true;

    LinuxTouchReader reader (fake, 90);
    check (reader.fd() == 103, "partial: connected");

    // Prepare a full SYN_REPORT input_event
    struct input_event synEv;
    std::memset (&synEv, 0, sizeof synEv);
    synEv.time.tv_sec = 1;
    synEv.time.tv_usec = 500000;
    synEv.type = TouchDecoder::kEvSyn;
    synEv.code = TouchDecoder::kSynReport;
    synEv.value = 0;

    const auto* synBytes = reinterpret_cast<const uint8_t*> (&synEv);
    const size_t totalBytes = sizeof synEv;
    const size_t chunk1 = 10;

    // Send Down events normally first
    fake.pushInputEvent (3, TouchDecoder::kEvAbs, TouchDecoder::kAbsMtSlot, 0, 1000);
    fake.pushInputEvent (3, TouchDecoder::kEvAbs, TouchDecoder::kAbsMtTrackingId, 1, 1000);
    fake.pushInputEvent (3, TouchDecoder::kEvAbs, TouchDecoder::kAbsMtPositionX, 100, 1000);
    fake.pushInputEvent (3, TouchDecoder::kEvAbs, TouchDecoder::kAbsMtPositionY, 200, 1000);

    // Now push only Chunk 1 of the SYN_REPORT event (incomplete record)
    fake.nodes[3].stream.insert (fake.nodes[3].stream.end(), synBytes, synBytes + chunk1);

    std::vector<TouchEvent> events;
    reader.poll (0, [&] (const TouchEvent& ev) { events.push_back (ev); });
    check (events.empty(), "partial: no TouchEvent emitted on partial input_event read");

    // Now push Chunk 2 (the rest of the SYN_REPORT event)
    fake.nodes[3].stream.insert (fake.nodes[3].stream.end(), synBytes + chunk1, synBytes + totalBytes);

    reader.poll (0, [&] (const TouchEvent& ev) { events.push_back (ev); });
    check (events.size() == 1 && events[0].kind == TouchEvent::Down, "partial: complete record parsed after second chunk");
}

// -------------------------------------------------------------------------------------------------
// Main
// -------------------------------------------------------------------------------------------------

int main()
{
    std::printf ("--- TouchDecoder Unit Tests ---\n");
    testTap();
    testDrag();
    testTwoFingers();
    testAllRotationsWithCornerPoints();
    testSynDropped();
    testScaledRanges();
    testGeometryAgreement();
    testOneFrameTap();
    testHandlerNeedsNoQueue();
    testTrackingIdReplaced();
    testOutOfRangeClamp();
    testTimeStamps();

    std::printf ("--- LinuxTouchReader Unit Tests (with Fake Syscalls) ---\n");
    testLinuxReaderDiscovery();
    testLinuxReaderEagain();
    testLinuxReaderDeviceRemovedAndRefound();
    testLinuxReaderPartialRecordRead();

    std::printf (g_fails ? "touch_host: FAILED (%d)\n" : "touch_host: OK\n", g_fails);
    return g_fails ? 1 : 0;
}
