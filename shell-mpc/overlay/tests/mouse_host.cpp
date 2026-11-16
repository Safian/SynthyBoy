// SynthyBoy - MPC overlay tests: a USB mouse on the plugin-drawn pages. The evdev decoder, the reader with fake nodes (found by capabilities, several mice, hot plug, never grabbed), the hot spot and the mapping
// of the host's pointer plane to the UI (rotation 90 and 270), the router (click, drag, wheel, no pointer), the wheel on the library's components (knob, number, stepper, list, scrolled list), and the whole
// chain in the controller with a fake mouse and a fake pointer plane: click, drag of a knob, wheel on a knob and a number field, the pointer over the host's area.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <array>
#include <cmath>
#include <cstring>

#include "../mouse_linux.h"
#include "../mouse_router.h"
#include "fakes.h"
#include "fixtures/legacy_layout.h"

using namespace ovt;

namespace
{
struct Ev { uint16_t type, code; int32_t value; };
constexpr uint16_t kSyn = 0, kKey = 1, kRel = 2;
constexpr uint16_t kReport = 0, kBtnLeft = 0x110, kBtnRight = 0x111, kRelX = 0, kRelY = 1, kWheel = 8;

/** Collects the events of a decoder. */
struct Sink
{
    std::vector<MouseEvent> v;
    void operator() (const MouseEvent& e) { v.push_back (e); }
};

// ---- fake evdev nodes -------------------------------------------------------------------------------------------------------------------------------------------------------------------
class FakeSys final : public ITouchSyscalls
{
public:
    struct Node
    {
        bool exists = false, rel = false, relX = false, relY = false, key = false, btnLeft = false, abs = false;
        std::vector<uint8_t> stream;
        size_t off = 0;
        int readErrno = 0;
    };
    Node nodes[32];
    uint64_t clock = 1000;
    int grabs = 0, writes = 0, opens = 0, closes = 0;
    int openFlags[32] = {};

    void addMouse (int i) { Node& n = nodes[i]; n.exists = n.rel = n.relX = n.relY = n.key = n.btnLeft = true; }
    void push (int i, const std::vector<Ev>& evs)
    {
        for (const Ev& e : evs)
        {
            struct input_event ie;
            std::memset (&ie, 0, sizeof ie);
            ie.time.tv_sec = 5;
            ie.time.tv_usec = 7;
            ie.type = e.type;
            ie.code = e.code;
            ie.value = e.value;
            const uint8_t* p = reinterpret_cast<const uint8_t*> (&ie);
            nodes[i].stream.insert (nodes[i].stream.end(), p, p + sizeof ie);
        }
    }
    int open (const char* path, int flags) override
    {
        int i = -1;
        if (std::sscanf (path, "/dev/input/event%d", &i) != 1 || i < 0 || i >= 32 || ! nodes[i].exists)
        {
            errno = ENOENT;
            return -1;
        }
        ++opens;
        openFlags[i] = flags;
        return 100 + i;
    }
    int close (int fd) override { ++closes; (void) fd; return 0; }
    ssize_t read (int fd, void* buf, size_t count) override
    {
        Node& n = nodes[fd - 100];
        if (! n.exists || n.readErrno != 0)
        {
            errno = n.readErrno != 0 ? n.readErrno : ENODEV;
            return -1;
        }
        if (n.off >= n.stream.size())
        {
            errno = EAGAIN;
            return -1;
        }
        const size_t k = std::min (count, n.stream.size() - n.off);
        std::memcpy (buf, n.stream.data() + n.off, k);
        n.off += k;
        return static_cast<ssize_t> (k);
    }
    int ioctl (int fd, unsigned long request, void* arg) override
    {
        Node& n = nodes[fd - 100];
        if ((request & 0xffff) == 0x4590)
        {
            ++grabs;
            errno = EINVAL;
            return -1;
        }
        const unsigned long base = request & ~0x1fff0000ul;
        uint8_t* bits = static_cast<uint8_t*> (arg);
        auto set = [&] (int b) { bits[b / 8] |= static_cast<uint8_t> (1u << (b % 8)); };
        if (base == 0x80004520ul)   // EVIOCGBIT (0): which event types
        {
            if (n.rel) set (kRel);
            if (n.key) set (kKey);
            if (n.abs) set (3);
            return 0;
        }
        if (base == (0x80004520ul | kRel))
        {
            if (n.relX) set (kRelX);
            if (n.relY) set (kRelY);
            return 0;
        }
        if (base == (0x80004520ul | kKey))
        {
            if (n.btnLeft) set (kBtnLeft);
            return 0;
        }
        errno = EINVAL;
        return -1;
    }
    int poll (struct pollfd*, nfds_t, int) override { return 0; }
    uint64_t nowMs() override { return clock; }
};

/** A fake pointer plane: where the host's pointer is on the panel. */
struct FakePointer final : IPointerSource
{
    PointerPlane p;
    int reads = 0;
    PointerPlane read() override { ++reads; return p; }
};

struct FakeMouse final : IMouseInput
{
    std::vector<MouseEvent> queue;
    int drains = 0, polls = 0;
    void poll (const std::function<void (const MouseEvent&)>& h) override { ++polls; for (const MouseEvent& e : queue) h (e); queue.clear(); }
    void drain() override { ++drains; queue.clear(); }
    void down() { MouseEvent e; e.kind = MouseEvent::Down; queue.push_back (e); }
    void up() { MouseEvent e; e.kind = MouseEvent::Up; queue.push_back (e); }
    void move() { MouseEvent e; e.kind = MouseEvent::Move; queue.push_back (e); }
    void wheel (int n) { MouseEvent e; e.kind = MouseEvent::Wheel; e.notches = n; queue.push_back (e); }
};
} // namespace

int main()
{
    // ---- the decoder ----
    {
        MouseDecoder d;
        Sink s;
        auto feed = [&] (uint16_t t, uint16_t c, int32_t v) { d.feed (t, c, v, 1, 0, s); };
        feed (kKey, kBtnLeft, 1);
        check (s.v.size() == 1 && s.v[0].kind == MouseEvent::Down && d.buttonDown(), "BTN_LEFT down: a Down at once");
        feed (kKey, kBtnLeft, 1);
        check (s.v.size() == 1, "a repeat of the same state: nothing");
        feed (kRel, kRelX, 4); feed (kRel, kRelY, -2);
        check (s.v.size() == 1, "movement is held back until the report");
        feed (kSyn, kReport, 0);
        check (s.v.size() == 2 && s.v[1].kind == MouseEvent::Move, "report with movement while the button is held: one Move");
        feed (kKey, kBtnLeft, 0);
        check (s.v.size() == 3 && s.v[2].kind == MouseEvent::Up && ! d.buttonDown(), "BTN_LEFT up: an Up");
        feed (kRel, kRelX, 4); feed (kSyn, kReport, 0);
        check (s.v.size() == 4 && s.v[3].kind == MouseEvent::Move, "movement with no button: a Move too (the router only reads the pointer's position for the status; the pages get nothing)");
        feed (kKey, kBtnRight, 1);
        check (s.v.size() == 4, "another button: not ours");
        feed (kRel, kWheel, 1); feed (kRel, kWheel, 2); feed (kSyn, kReport, 0);
        check (s.v.size() == 5 && s.v[4].kind == MouseEvent::Wheel && s.v[4].notches == 3, "wheel notches of one report are summed (3)");
        feed (kRel, kWheel, -1); feed (kSyn, kReport, 0);
        check (s.v.size() == 6 && s.v[5].notches == -1, "wheel the other way: -1");
        feed (kRel, kWheel, 1); feed (kSyn, MouseDecoder::kSynDropped, 0); feed (kSyn, kReport, 0);
        check (s.v.size() == 6, "SYN_DROPPED: the half-done report is thrown away");
        feed (kKey, kBtnLeft, 1); feed (kRel, kRelX, 1); feed (kRel, kWheel, 1); feed (kSyn, kReport, 0);
        check (s.v.size() == 9 && s.v[6].kind == MouseEvent::Down && s.v[7].kind == MouseEvent::Wheel && s.v[8].kind == MouseEvent::Move, "a report with the wheel and a drag: both, the wheel first");
    }
    // ---- the reader ----
    {
        FakeSys sys;
        sys.addMouse (3);
        sys.nodes[5].exists = sys.nodes[5].abs = true;                 // a touch screen: no relative axes
        sys.nodes[6].exists = sys.nodes[6].key = true;                 // a keyboard
        sys.nodes[7].exists = sys.nodes[7].rel = sys.nodes[7].relX = sys.nodes[7].relY = sys.nodes[7].key = true;   // relative axes but no left button (a trackpoint's wheel, a knob)
        LinuxMouseReader r (sys);
        Sink s;
        auto poll = [&] { r.poll ([&] (const MouseEvent& e) { s.v.push_back (e); }); };
        poll();
        check (r.deviceCount() == 1 && r.scans() == 1, "one mouse found by its capabilities (REL_X, REL_Y, BTN_LEFT); the touch screen, the keyboard and the knob are not mice");
        check ((sys.openFlags[3] & O_RDONLY) == O_RDONLY && (sys.openFlags[3] & O_NONBLOCK) && (sys.openFlags[3] & O_WRONLY) == 0 && (sys.openFlags[3] & O_RDWR) == 0, "the node is opened read only and non blocking");
        sys.push (3, { { kKey, kBtnLeft, 1 }, { kSyn, kReport, 0 }, { kRel, kWheel, 2 }, { kSyn, kReport, 0 }, { kKey, kBtnLeft, 0 }, { kSyn, kReport, 0 } });
        poll();
        check (s.v.size() == 3 && s.v[0].kind == MouseEvent::Down && s.v[1].kind == MouseEvent::Wheel && s.v[1].notches == 2 && s.v[2].kind == MouseEvent::Up, "the events of the node come through (down, wheel 2, up)");
        // hot plug: a second mouse appears
        sys.addMouse (9);
        sys.clock += 500;
        poll();
        check (r.deviceCount() == 1, "a mouse plugged in is not looked for before the next scan (every 2 s)");
        sys.clock += 2000;
        poll();
        check (r.deviceCount() == 2, "... found at the next scan: two mice, no restart");
        s.v.clear();
        sys.push (9, { { kRel, kWheel, -1 }, { kSyn, kReport, 0 } });
        sys.push (3, { { kRel, kWheel, 1 }, { kSyn, kReport, 0 } });
        poll();
        check (s.v.size() == 2 && s.v[0].device != s.v[1].device, "both mice are read (the events carry the mouse's number)");
        // the first one is unplugged with its button down
        s.v.clear();
        sys.push (3, { { kKey, kBtnLeft, 1 }, { kSyn, kReport, 0 } });
        poll();
        sys.nodes[3].exists = false;
        poll();
        check (r.deviceCount() == 1 && s.v.size() == 2 && s.v[1].kind == MouseEvent::Up, "a mouse unplugged with its button down: dropped, the touch ends with an Up");
        sys.nodes[9].exists = false;
        poll();
        check (r.deviceCount() == 0, "the last mouse unplugged: none, nothing breaks");
        sys.addMouse (3);
        sys.clock += 2500;
        poll();
        check (r.deviceCount() == 1, "plugged in again: found");
        // drain: what happened while the pages were away is thrown away
        sys.push (3, { { kRel, kWheel, 5 }, { kSyn, kReport, 0 } });
        s.v.clear();
        r.drain();
        poll();
        check (s.v.empty(), "drain: the events of the time the pages were not on screen do not arrive later");
        check (sys.grabs == 0 && sys.writes == 0, "no EVIOCGRAB and no write, ever: the host keeps getting every event");
        const std::string d = r.describe();
        check (d.find ("mouse_devices: 1") != std::string::npos && d.find ("event3") != std::string::npos, "the status lines name the devices");
    }
    // ---- the hot spot and the mapping (rotation 90: Key 61, 270: XL) ----
    {
        HotSpot h90 = defaultHotSpot (90, 64, 64), h270 = defaultHotSpot (270, 64, 64), h0 = defaultHotSpot (0, 64, 64), h180 = defaultHotSpot (180, 64, 64);
        check (h90.x == 0 && h90.y == 63 && h270.x == 63 && h270.y == 0 && h0.x == 0 && h0.y == 0 && h180.x == 63 && h180.y == 63, "the default hot spot: the corner where the rotation maps the UI's top left (90: bottom left, 270: top right)");
        for (int rot : { 90, 270, 0, 180 })
        {
            const int pw = (rot == 90 || rot == 270) ? 800 : 1280, ph = (rot == 90 || rot == 270) ? 1280 : 800;
            bool ok = true;
            const int pts[4][2] = { { 0, 0 }, { 640, 400 }, { 1279, 799 }, { 100, 700 } };
            for (const auto& pt : pts)
            {
                PointerPlane p;
                p.valid = true;
                p.w = p.h = 64;
                const HotSpot h = defaultHotSpot (rot, 64, 64);
                int cx, cy;
                uiPointToPanel (rot, pt[0], pt[1], pw, ph, cx, cy);
                p.x = cx - h.x;
                p.y = cy - h.y;
                int ux, uy;
                pointerToUi (p, h, rot, pw, ph, ux, uy);
                ok = ok && ux == pt[0] && uy == pt[1];
            }
            check (ok, "rotation " + std::to_string (rot) + ": the arrow tip at a UI point maps back to it");
        }
        HotSpot m;
        check (measuredHotSpot (90, 800, 1280, 64, 64, m) && m.x == 0 && m.y == 63 && std::string (m.source) == "measured" && m.x == h90.x && m.y == h90.y, "the Key 61 (rotation 90, 800 x 1280): the measured hot spot (0, 63), as the corner rule says");
        check (! measuredHotSpot (270, 800, 1280, 64, 64, m), "the XL (rotation 270): not measured, the corner rule is used");
        int x = 0, y = 0;
        check (parseHotSpot ("12 40", x, y) && x == 12 && y == 40 && parseHotSpot ("3,60\n", x, y) && x == 3 && y == 60, "the override file text: two numbers");
        check (! parseHotSpot ("", x, y) && ! parseHotSpot ("abc", x, y) && ! parseHotSpot ("5", x, y) && ! parseHotSpot ("5 9999", x, y), "... anything else is no override");
    }
    // ---- the router ----
    {
        FakeMouse m;
        FakePointer p;
        MouseRouter r (m, p);
        r.configure (800, 1280, 90);
        std::vector<TouchEvent> touches;
        std::vector<std::array<int, 3>> wheels;
        auto sinkT = [&] (const TouchEvent& e) { touches.push_back (e); };
        auto sinkW = [&] (int x, int y, int n) { wheels.push_back ({ x, y, n }); };
        auto place = [&] (int ux, int uy)
        {
            const HotSpot h = defaultHotSpot (90, 64, 64);
            int cx, cy;
            uiPointToPanel (90, ux, uy, 800, 1280, cx, cy);
            p.p.valid = true; p.p.id = 40; p.p.w = p.p.h = 64; p.p.x = cx - h.x; p.p.y = cy - h.y;
        };
        place (300, 200);
        m.down();
        r.poll (0.0, 33.0, sinkT, sinkW);
        check (touches.size() == 1 && touches[0].kind == TouchEvent::Down && touches[0].x == 300 && touches[0].y == 200, "left button down: a touch Down at the pointer's UI position (300, 200)");
        place (310, 240);
        m.move();
        r.poll (10.0, 33.0, sinkT, sinkW);
        check (touches.size() == 2 && touches[1].kind == TouchEvent::Move && touches[1].x == 310 && touches[1].y == 240, "movement with the button held: a Move at the new position");
        place (320, 250);
        m.move();
        r.poll (20.0, 33.0, sinkT, sinkW);
        check (touches.size() == 2, "... the position is read at most once per picture (33 ms): nothing yet");
        r.poll (45.0, 33.0, sinkT, sinkW);
        check (touches.size() == 3 && touches[2].x == 320 && touches[2].y == 250, "... the pending move comes with the next picture, at the position of that moment");
        r.poll (80.0, 33.0, sinkT, sinkW);
        check (touches.size() == 3, "... and nothing more while the pointer stands still");
        place (330, 260);
        m.up();
        r.poll (90.0, 33.0, sinkT, sinkW);
        check (touches.size() == 4 && touches[3].kind == TouchEvent::Up && touches[3].x == 330 && touches[3].y == 260 && ! r.buttonDown(), "button up: a touch Up where the pointer is");
        m.wheel (-2);
        r.poll (100.0, 33.0, sinkT, sinkW);
        check (wheels.size() == 1 && wheels[0][0] == 330 && wheels[0][1] == 260 && wheels[0][2] == -2, "the wheel: at the pointer's position, with its notches");
        p.p.valid = false;
        m.down();
        r.poll (110.0, 33.0, sinkT, sinkW);
        m.wheel (1);
        r.poll (120.0, 33.0, sinkT, sinkW);
        check (touches.size() == 4 && wheels.size() == 1 && ! r.buttonDown(), "no pointer on the screen (the host shows none): no touch and no wheel step");
        m.up();
        r.poll (130.0, 33.0, sinkT, sinkW);
        check (touches.size() == 4, "... and the button up that follows is nothing");
        place (500, 300);
        m.wheel (1);
        r.poll (135.0, 33.0, sinkT, sinkW);
        const std::string d = r.describe();
        check (d.find ("pointer_plane: 40") != std::string::npos && d.find ("pointer_hotspot: 0,63 (measured)") != std::string::npos && d.find ("pointer_ui_xy:") != std::string::npos && d.find ("mouse_wheel_notches: 0") != std::string::npos, "the status lines have the plane, the mapped UI position and the hot spot");
        {   // the pointer moves with no button down: pointer_ui_xy follows it (at most once per picture), nothing reaches the pages
            const size_t t0 = touches.size();
            place (420, 260);
            m.move();
            r.poll (200.0, 33.0, sinkT, sinkW);
            int mx, my;
            pointerToUi (p.p, defaultHotSpot (90, 64, 64), 90, 800, 1280, mx, my);
            check (r.describe().find ("pointer_ui_xy: " + std::to_string (mx) + "," + std::to_string (my) + "\n") != std::string::npos && touches.size() == t0, "a move without a button: the status has the pointer's position, no touch is sent");
            const int reads = p.reads;
            place (440, 280);
            m.move();
            r.poll (210.0, 33.0, sinkT, sinkW);
            check (p.reads == reads, "a second move within a picture's time is not read at once (cheap while the mouse moves)");
            r.poll (240.0, 33.0, sinkT, sinkW);
            check (p.reads == reads + 1, "... it is read at the next picture time, so the last position shows");
        }
        // the debug override of the hot spot: the tip is somewhere else in the picture
        place (500, 300);
        r.setHotSpotOverride ("10 50");
        m.down();
        r.poll (140.0, 33.0, sinkT, sinkW);
        int ex, ey;
        PointerPlane pp = p.p;
        pointerToUi (pp, HotSpot { 10, 50, "file" }, 90, 800, 1280, ex, ey);
        check (touches.size() == 5 && touches[4].x == ex && touches[4].y == ey && ex != 500, "the override file moves the hot spot (" + std::to_string (touches[4].x) + "," + std::to_string (touches[4].y) + ")");
        m.up();
        r.poll (150.0, 33.0, sinkT, sinkW);
        r.idle();
        check (m.drains == 1 && ! r.buttonDown(), "idle: the mouse is drained");
    }
    // ---- the wheel on the library's components ----
    {
        using namespace synthyboy_mpc::ui;
        Knob k ("LEVEL");
        k.place ({ 0, 0, 100, 100 });
        k.state.value = 0.5;
        Change c;
        check (k.wheelOver (50, 50, 4, c) && c.kind == ChangeKind::Value && std::fabs (c.value - (0.5 + 4.0 / 128.0)) < 1e-9, "a knob: 4 notches = 4 Q-Link steps (4 / 128 of its range)");
        check (k.wheelOver (50, 50, -1, c) && std::fabs (c.value - (0.5 - 1.0 / 128.0)) < 1e-9, "... down: one step less");
        check (! k.wheelOver (150, 50, 1, c), "outside of the knob: not taken");
        k.setEnabled (false);
        check (! k.wheelOver (50, 50, 1, c), "a disabled control: not taken");
        NumberField n ("SEM", NumberField::Format::IntSigned);
        n.place ({ 0, 0, 100, 60 });
        n.state.min = -12; n.state.max = 12; n.state.value = 0;
        check (n.wheelOver (10, 10, 3, c) && c.kind == ChangeKind::Index && c.index == 15, "a number field (index from the minimum: 12 + 3): one whole step a notch");
        Stepper st;
        st.place ({ 0, 0, 200, 60 });
        st.state.count = 5; st.state.index = 2;
        check (st.wheelOver (10, 10, 1, c) && c.index == 3 && st.wheelOver (10, 10, -1, c) && c.index == 1, "a stepper: next / previous");
        UpDownStepper ud;
        ud.place ({ 0, 0, 80, 112 });
        ud.state.min = 1; ud.state.max = 16; ud.state.value = 4;
        check (ud.wheelOver (10, 10, 2, c) && c.index == 6, "an up / down stepper: two steps for two notches");
        static const char* const items[5] = { "a", "b", "c", "d", "e" };
        ListPopup lp;
        lp.place ({ 0, 0, 300, 300 });
        lp.state.items = items; lp.state.count = 5; lp.state.current = 2;
        check (lp.wheelOver (50, 50, 1, c) && c.index == 1, "a list: the wheel up goes up (the previous item)");
        check (lp.wheelOver (50, 50, -1, c) && c.index == 3, "... down: the next one");
    }
    // ---- the chain in the controller: fake mouse, fake pointer plane, the Play page's oscillator full screen ----
    for (int rot : { 90, 270 })
    {
        const std::string tag = "rotation " + std::to_string (rot) + ": ";
        TempDir dir;
        FakePlane plane;
        FakeFrame frame;
        CpuCurveRenderer renderer;
        FakeInstance inst;
        FakeMouse mouse;
        FakePointer ptr;
        frame.views = legacy::kViews;
        auto ctl = std::make_unique<Controller> (plane, frame, renderer, dir.paths(), Timing(), 10.0, nullptr, nullptr, legacy::kViews, legacy::kNumViews);
        ctl->setMouse (&mouse, &ptr);
        inst.set ("filter1.cutoff", 1000.0f);
        ctl->attach (&inst);
        double now = 0.0;
        auto run = [&] (double ms) { const double end = now + ms; while (now < end) { now += 10.0; ctl->tick (now); } };
        const int play1 = 14;
        inst.playSel[0].store (2);
        frame.setView (play1, true, rot);
        run (900);
        check (ctl->currentIndex() == play1 && ctl->rotation() == rot, tag + "the Play page is on screen, rotation found");
        auto* pv = static_cast<PlayView*> (const_cast<IView*> (ctl->viewByName ("play1")));
        run (200);
        const HotSpot hot = defaultHotSpot (rot, 64, 64);
        auto place = [&] (int ux, int uy)
        {
            int cx, cy;
            uiPointToPanel (rot, ux, uy, 800, 1280, cx, cy);
            ptr.p.valid = true; ptr.p.id = 40; ptr.p.w = ptr.p.h = 64; ptr.p.x = cx - hot.x; ptr.p.y = cy - hot.y;
        };
        // a click on the full-screen button
        const synthyboy_mpc::ui::Rect fb = pv->oscUi()->compact().full.rect();
        place (fb.x + fb.w / 2, fb.y + fb.h / 2);
        mouse.down();
        run (60);
        mouse.up();
        run (200);
        const int slot = ctl->slotOf (&inst);
        check (ctl->uiState (slot).level() == UiStack::Level::full, tag + "a click on the full-screen button opens the full screen (left button = a finger)");
        run (300);
        // a drag on the LEVEL knob
        const synthyboy_mpc::ui::Rect lv = pv->oscUi()->full().basic.level.rect();
        const float before = inst.get ("osc1.level");
        place (lv.x + lv.w / 2, lv.y + lv.h / 2);
        mouse.down();
        run (60);
        for (int i = 1; i <= 8; ++i)
        {
            place (lv.x + lv.w / 2, lv.y + lv.h / 2 - i * 8);
            mouse.move();
            run (50);
        }
        mouse.up();
        run (200);
        check (inst.get ("osc1.level") > before + 10.0f, tag + "a drag with the left button on LEVEL writes osc1.level (" + std::to_string (before) + " -> " + std::to_string (inst.get ("osc1.level")) + ")");
        // the wheel on the knob: one Q-Link step (1 / 128 of the range) a notch
        const synthyboy::ParamInfo* pi = synthyboy::findParamByKey ("osc1.level");
        const float v0 = inst.get ("osc1.level");
        place (lv.x + lv.w / 2, lv.y + lv.h / 2);
        mouse.wheel (4);
        run (100);
        const float v1 = inst.get ("osc1.level");
        const double stepPlain = (static_cast<double> (pi->max) - static_cast<double> (pi->min)) / 128.0;
        check (std::fabs ((v1 - v0) - 4.0 * stepPlain) < 0.05 * stepPlain * 4.0 + 1e-3, tag + "4 wheel notches over LEVEL: +4 Q-Link steps (" + std::to_string (v1 - v0) + " of " + std::to_string (4.0 * stepPlain) + ")");
        mouse.wheel (-4);
        run (100);
        check (std::fabs (inst.get ("osc1.level") - v0) < 0.05 * stepPlain * 4.0 + 1e-3, tag + "4 notches back: the value is back");
        // the wheel on a number field: one whole step a notch
        const synthyboy_mpc::ui::Rect sem = pv->oscUi()->full().tune.sem.rect();
        const float t0 = inst.get ("osc1.transpose");
        place (sem.x + sem.w / 2, sem.y + sem.h / 2);
        mouse.wheel (2);
        run (100);
        check (std::fabs ((inst.get ("osc1.transpose") - t0) - 2.0f) < 0.01f, tag + "2 notches over SEM: +2 semitones (" + std::to_string (inst.get ("osc1.transpose") - t0) + ")");
        // the wheel over nothing of ours (outside the tab: the host's top bar) takes nothing
        const unsigned long taken = ctl->wheelTaken(), events = ctl->wheelEvents();
        place (640, 20);
        mouse.wheel (3);
        run (100);
        check (ctl->wheelEvents() == events + 1 && ctl->wheelTaken() == taken, tag + "the wheel over the host's own part (above the tab): seen, not taken");
        // a click there starts no gesture either
        const unsigned long tt = ctl->touchTaken();
        mouse.down();
        run (60);
        mouse.up();
        run (100);
        check (ctl->touchTaken() == tt, tag + "a click over the host's part is not taken");
        // the status file
        run (1200);
        const std::string st = dir.read ("status.txt");
        check (st.find ("mouse_input: yes") != std::string::npos && st.find ("pointer_hotspot:") != std::string::npos && st.find ("pointer_plane_xy:") != std::string::npos && st.find ("wheel_taken:") != std::string::npos, tag + "the status file has the mouse, the pointer plane position, the hot spot and the wheel counters");
        // the pages go: the mouse is drained, not read for the pages
        frame.setView (play1, false, rot);
        run (500);
        const int drains = mouse.drains;
        run (100);
        check (mouse.drains > drains, tag + "no page on screen: the mouse is drained, its events do not wait for the next page");
        ctl->detach (&inst);
    }
    std::printf (g_fails ? "mouse_host: FAILED (%d)\n" : "mouse_host: OK\n", g_fails);
    return g_fails ? 1 : 0;
}
