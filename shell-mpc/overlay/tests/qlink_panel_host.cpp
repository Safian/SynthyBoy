// SynthyBoy - MPC overlay tests: replays of the host's Q-Link status pop-up against the controller (the key of panel_key.h at work). The pop-up opens when a knob is TOUCHED (also without any turn: no
// parameter write), slides in from the right edge, stays while the knob is touched, slides out; the host repaints its whole back buffer on every flip; the standby dim repaints the picture at half
// brightness. The rule under test: the key never leaves the strip cut while the host shows its plain page there, and never leaves the host's panel uncovered once the reference is the plain page.
// Also the option of the host that switches the pop-up off (qlink_setting.h): with the setting 0 the key is off entirely.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <fstream>
#include <functional>

#include "fakes.h"
#include "fixtures/legacy_layout.h"

using namespace ovt;

namespace
{
constexpr int kPlain = 0x202020, kPanelFill = 0x1e1d1e, kLineY0 = 0, kLineY1 = 740;

/** The host as the key sees it: the strip of the tab, the markers of the page, the panel (a band from `left` to the right edge: a black line of two pixels, the fill right of it) and the dim. */
struct Host
{
    FakeFrame& f;
    int left = 1280;       // the panel's left edge (1280: no panel)
    bool page = false;     // the page's markers and instance picture are on screen
    bool dim = false;      // the standby dim: everything at half brightness (the markers of the page are not recognised any more)

    void paintX (int x0, int x1, bool panel)
    {
        for (int x = std::max (840, x0); x < std::min (1280, x1); ++x)
            for (int y = kLineY0; y < kLineY1; ++y)
            {
                int px, py;
                uiPointToPanel (90, x, y, 800, 1280, px, py);
                const uint32_t plain = dim ? 0x101010u : kPlain;
                f.setPixel (px, py, panel ? (x < left + 2 ? 0x000000u : (dim ? 0x0f0e0fu : kPanelFill)) : plain);
            }
    }
    void all()
    {
        paintX (840, 1280, false);
        paintX (left, 1280, true);
        markers();
    }
    void markers()
    {
        const bool on = page && ! dim;
        f.setAll (on, 90);
        // the panel covers whatever of the markers lies right of its left edge
        if (left < 1280)
            paintX (left, 1280, true);
    }
    void setPage (bool on) { page = on; markers(); }
    void setDim (bool on) { dim = on; all(); }
    void setLeft (int l)
    {
        const int old = left;
        left = l;
        if (l < old)
            paintX (l, old + 2, true);
        else if (l > old)
        {
            paintX (old, l, false);
            paintX (l, l + 2, true);
            markers();
        }
        else
            return;
        if (l > old)
            markers();
    }
};

struct Rig
{
    TempDir dir;
    FakePlane plane;
    FakeFrame frame;
    CpuCurveRenderer renderer;
    FakeInstance inst;
    std::unique_ptr<Controller> ctl;
    Host host { frame };
    double now = 0.0;
    int maxEdge = PanelKey::kNoKey;   // the lowest cut seen since the last reset (the deepest into the strip)
    int stuckTicks = 0;               // ticks with the strip plain for a while and the picture still cut
    int hiddenTicks = 0;              // ticks with the page on the host and our plane off
    int uncoveredTicks = 0, overTicks = 0, blinkTicks = 0, stuckRun = 0;   // ticks with the panel drawn and our picture over it / our page gone
    double plainSince = 0.0;
    bool panelSeen = false;

    Rig()
    {
        frame.views = legacy::kViews;
        ctl = std::make_unique<Controller> (plane, frame, renderer, dir.paths(), Timing(), 10.0, nullptr, nullptr, legacy::kViews, legacy::kNumViews);
        inst.set ("filter1.cutoff", 1000.0f);
        ctl->attach (&inst);
        host.all();
    }
    /** One tick of 10 ms; the monitor looks at the result. */
    void tick (bool write = false)
    {
        now += 10.0;
        if (write)
            inst.qWrites.fetch_add (1);
        ctl->tick (now);
        const bool panelDrawn = host.left < 1280 || host.dim;
        if (panelDrawn)
        {
            plainSince = now;
            panelSeen = true;
        }
        // the hard rule: the host shows its plain page in the strip for 300 ms (three checks of the page): the picture is not cut
        if (host.page && ! host.dim && now - plainSince >= 300.0 && now > 800.0 && ctl->keyEdge() < PanelKey::kNoKey)
            stuckTicks += ++stuckRun > 5 ? 1 : 0;   // the key follows the pixels with one tick of lag; a cut that lasts longer than five ticks (50 ms) is stuck
        else
            stuckRun = 0;
        if (host.page && ! host.dim && now - plainSince >= 300.0 && now > 800.0 && ! plane.planeOn)
            ++hiddenTicks;
        // the panel drawn and our picture over it (the cut is right of its edge, or no cut), one check later than the draw
        if (host.page && ! host.dim && host.left < 1250 && ctl->keyEdge() > host.left + 4 && ++overTicks > 3)
            ++uncoveredTicks;
        if (! (host.page && ! host.dim && host.left < 1250 && ctl->keyEdge() > host.left + 4))
            overTicks = 0;
        // the panel drawn and our page gone (a blink)
        if (host.page && ! host.dim && host.left < 1250 && ! plane.planeOn)
            ++blinkTicks;
    }
    void run (double ms, bool write = false)
    {
        const double end = now + ms;
        while (now < end)
            tick (write);
    }
    /** The panel slides in over `ms` (from the right edge to x 1024), with a write of the host to a Q proxy at every tick when `write`. */
    void slideIn (double ms, bool write = false)
    {
        const int steps = static_cast<int> (ms / 10.0);
        for (int i = 1; i <= steps; ++i)
        {
            host.setLeft (1280 - (1280 - 1024) * i / steps);
            tick (write);
        }
    }
    void slideOut (double ms, bool write = false)
    {
        const int steps = static_cast<int> (ms / 10.0);
        for (int i = 1; i <= steps; ++i)
        {
            host.setLeft (1024 + (1280 - 1024) * i / steps);
            tick (write);
        }
        host.setLeft (1280);
    }
    unsigned long field (const char* k) const
    {
        const std::string t = dir.read ("status.txt");
        const std::string key = std::string (k) + ": ";
        const size_t p = t.find (key);
        return p == std::string::npos ? 0ul : std::strtoul (t.c_str() + p + key.size(), nullptr, 10);
    }
    void resetMonitor() { stuckTicks = hiddenTicks = uncoveredTicks = overTicks = blinkTicks = 0; }
};
} // namespace

int main()
{
    // ---- the pop-up of a touch, with no turn at all (no write of the host to a Q proxy): up, held, away, many times, at the places of a human's timing ----
    {
        Rig r;
        r.host.setPage (true);
        r.run (2500);
        check (r.field ("key_reference_taken") == 1, "the page is settled: the reference is taken once");
        const double holds[6] = { 400.0, 1500.0, 90.0, 3000.0, 700.0, 250.0 };
        const double gaps[6] = { 150.0, 700.0, 40.0, 1200.0, 90.0, 600.0 };
        for (int i = 0; i < 6; ++i)
        {
            r.slideIn (250.0);
            r.run (holds[i]);
            r.slideOut (250.0);
            r.run (gaps[i]);
        }
        r.run (2000);
        check (r.uncoveredTicks == 0 && r.blinkTicks == 0, "... and the pop-up was never left uncovered by our picture (" + std::to_string (r.uncoveredTicks) + " ticks) and the page never blinked off under it (" + std::to_string (r.blinkTicks) + " ticks)");
        check (r.field ("view_hides") == 0 && r.field ("key_reference_retakes") == 0 && r.field ("key_reference_taken") == 1, "... the reference was neither dropped nor taken again by a pop-up leaving (hides " + std::to_string (r.field ("view_hides")) + ", retakes " + std::to_string (r.field ("key_reference_retakes")) + ", taken " + std::to_string (r.field ("key_reference_taken")) + ")");
        check (r.stuckTicks == 0, "touch only, six pop-ups at changing timing: the picture was never left cut while the host showed its plain page (" + std::to_string (r.stuckTicks) + " ticks)");
        check (r.hiddenTicks == 0, "... and the page never went off while the host showed its plain page (" + std::to_string (r.hiddenTicks) + " ticks)");
        check (r.ctl->keyEdge() == PanelKey::kNoKey, "... and at the end it is not cut");
    }
    // ---- the pop-up that comes while the reference is being taken again: after a retake the pop-up slides in at the moment the page has been whole for the settle time ----
    {
        Rig r;
        r.host.setPage (true);
        r.run (1200);
        // the strip changes completely (another host page): the reference is dropped, then every moment of the settle window is tried: the pop-up slides in at that moment, touch only
        bool ok = true;
        for (int offset = 300; offset <= 1500; offset += 100)
        {
            Rig q;
            q.host.setPage (true);
            q.run (1500);
            // a new project / another skin: the whole strip is another one
            for (int x = 840; x < 1280; ++x)
                for (int y = 112; y < 740; ++y)
                {
                    int px, py;
                    uiPointToPanel (90, x, y, 800, 1280, px, py);
                    q.frame.setPixel (px, py, 0x404040u);
                }
            q.host.markers();
            q.run (offset);
            q.slideIn (250.0);
            q.run (800.0);
            q.slideOut (250.0);
            q.run (200.0);
            // the strip under the pop-up is the plain page of the new picture now: back to it
            q.run (2500);
            ok = ok && q.stuckTicks == 0;
            ok = ok && q.hiddenTicks == 0;
            if (q.stuckTicks != 0 || q.hiddenTicks != 0)
                std::printf ("      offset %d: stuck %d ticks, hidden %d ticks\n", offset, q.stuckTicks, q.hiddenTicks);
        }
        check (ok, "the pop-up slides in at every moment of the settle window after a retake (300 .. 1500 ms): never stuck cut, never hidden");
    }
    // ---- the page is entered and a pop-up comes at every moment of the first two seconds (touch only): the reference is never taken from a strip with the pop-up part way in ----
    {
        bool ok = true;
        for (int offset = 0; offset <= 2000; offset += 50)
        {
            Rig q;
            q.host.setPage (false);
            q.run (300);
            q.host.setPage (true);
            q.run (offset);
            q.slideIn (250.0);
            q.run (500.0);
            q.slideOut (250.0);
            q.run (3000);
            if (q.stuckTicks != 0 || q.hiddenTicks != 0 || q.blinkTicks != 0 || q.uncoveredTicks != 0 || q.ctl->keyEdge() != PanelKey::kNoKey)
            {
                ok = false;
                std::printf ("      pop-up %d ms after the page: stuck %d, hidden %d, blink %d, uncovered %d, edge %d\n", offset, q.stuckTicks, q.hiddenTicks, q.blinkTicks, q.uncoveredTicks, q.ctl->keyEdge());
            }
        }
        check (ok, "the pop-up comes at every moment of the first two seconds of a page (50 ms apart): never stuck cut, never hidden, no blink, never uncovered, not cut at the end");
    }
    // ---- a panel that stops part way (the host's slide stalls) while the reference is being taken: the reference holds the panel's line, the panel then leaves: the cut is let go within 500 ms ----
    {
        Rig r;
        r.host.setPage (true);
        r.host.setLeft (1240);       // the page entered with the panel's line at 1240 (the right marker is still visible), and the line stays there
        r.run (3000);
        r.host.setLeft (1280);
        r.run (500);
        check (r.ctl->keyEdge() == PanelKey::kNoKey, "the reference was taken with the line at 1240 and the panel leaves: the cut is let go within 500 ms (edge " + std::to_string (r.ctl->keyEdge()) + ")");
        r.run (2500);
        r.slideIn (250.0);
        r.run (400);
        check (r.ctl->keyEdge() >= 1020 && r.ctl->keyEdge() <= 1024, "... and the next pop-up is followed (cut at " + std::to_string (r.ctl->keyEdge()) + ")");
    }
    // ---- the pop-up that is already up when the page is entered ----
    {
        Rig r;
        r.host.setPage (false);
        r.host.setLeft (1024);                  // the pop-up is fully in on the other tab
        r.run (500);
        r.host.setPage (true);                  // the page is entered under the pop-up (a tab key pressed while a knob is touched)
        r.run (1500);
        check (r.ctl->state() == "visible" || r.ctl->keyEdge() <= 1024 + 2, "the page entered under a pop-up: shown, or held by the key (state " + r.ctl->state() + ")");
        r.slideOut (250.0);
        r.run (2500);
        check (r.stuckTicks == 0 && r.hiddenTicks == 0, "the pop-up leaves: the picture is not cut and the page is on (stuck " + std::to_string (r.stuckTicks) + ", hidden " + std::to_string (r.hiddenTicks) + ")");
        check (r.ctl->keyEdge() == PanelKey::kNoKey, "... not cut at the end");
        // and again: the reference is taken from the plain page, the next pop-up is followed
        r.resetMonitor();
        r.slideIn (250.0);
        r.run (500);
        check (r.ctl->keyEdge() < PanelKey::kNoKey && r.ctl->keyEdge() >= 1020 && r.ctl->keyEdge() <= 1024, "the next pop-up is followed: cut at " + std::to_string (r.ctl->keyEdge()));
        r.slideOut (250.0);
        r.run (1500);
        check (r.stuckTicks == 0, "... and gone again");
    }
    // ---- a long turn: writes of the host to the Q proxies for seconds, the pop-up held, then it leaves ----
    {
        Rig r;
        r.host.setPage (true);
        r.run (2000);
        r.slideIn (250.0, true);
        r.run (6000, true);
        check (r.ctl->keyEdge() >= 1020 && r.ctl->keyEdge() <= 1024, "a long turn (6 s of writes): the picture stays cut at the pop-up's edge (" + std::to_string (r.ctl->keyEdge()) + ")");
        check (r.ctl->state() == "visible", "... and stays on");
        r.slideOut (250.0, true);
        r.run (200);
        r.slideIn (250.0);       // touched again at once, no turn this time
        r.run (300);
        r.slideOut (250.0);
        r.run (3000);
        check (r.stuckTicks == 0 && r.hiddenTicks == 0, "after the long turn, a quick second touch, away: never stuck, never hidden (stuck " + std::to_string (r.stuckTicks) + ", hidden " + std::to_string (r.hiddenTicks) + ")");
    }
    // ---- the pop-up leaves while the page is still held by the key, and comes back at every moment of the slide-out ----
    {
        bool ok = true;
        for (int out = 0; out <= 25; ++out)
        {
            Rig r;
            r.host.setPage (true);
            r.run (2000);
            r.slideIn (250.0);
            r.run (1000);
            for (int i = 1; i <= out; ++i)
            {
                r.host.setLeft (1024 + (1280 - 1024) * i / 25);
                r.tick();
            }
            r.slideIn (250.0);                      // touched again at slide-out step `out`
            r.run (800);
            r.slideOut (250.0);
            r.run (3000);
            if (r.stuckTicks != 0 || r.hiddenTicks != 0)
            {
                ok = false;
                std::printf ("      re-touch at slide-out step %d: stuck %d, hidden %d\n", out, r.stuckTicks, r.hiddenTicks);
            }
        }
        check (ok, "the pop-up comes back at every moment of its slide-out: never stuck cut, never hidden");
    }
    // ---- the standby dim: the whole picture at half brightness: the page goes, the strip is not the reference any more; back from the dim, and the pop-up in the middle of it ----
    {
        Rig r;
        r.host.setPage (true);
        r.run (2000);
        r.host.setDim (true);
        r.run (3000);
        r.host.setDim (false);
        r.run (2500);
        check (r.stuckTicks == 0 && r.hiddenTicks == 0, "the dim and back: the picture is not cut once the page is plain (stuck " + std::to_string (r.stuckTicks) + ", hidden " + std::to_string (r.hiddenTicks) + ")");
        r.slideIn (250.0);
        r.run (600);
        check (r.ctl->keyEdge() >= 1020 && r.ctl->keyEdge() <= 1024, "after the dim the pop-up is followed again (cut at " + std::to_string (r.ctl->keyEdge()) + ")");
        // the dim comes while the pop-up is up (a touch that stays: the display goes dim after a time), then the touch ends, then the dim ends
        r.host.setDim (true);
        r.run (1500);
        r.host.setLeft (1280);
        r.host.all();
        r.run (500);
        r.host.setDim (false);
        r.run (3000);
        check (r.stuckTicks == 0 && r.hiddenTicks == 0, "dim with the pop-up up, the pop-up gone during the dim, the dim ends: not cut (stuck " + std::to_string (r.stuckTicks) + ", hidden " + std::to_string (r.hiddenTicks) + ")");
        // the pop-up slides in at the moment the dim ends (the host repaints the dimmed strip with the pop-up on it)
        r.resetMonitor();
        r.host.setDim (true);
        r.run (1000);
        r.host.setDim (false);
        r.slideIn (250.0);
        r.run (600);
        r.slideOut (250.0);
        r.run (3000);
        check (r.stuckTicks == 0 && r.hiddenTicks == 0, "the pop-up comes as the dim ends: never stuck, never hidden (stuck " + std::to_string (r.stuckTicks) + ", hidden " + std::to_string (r.hiddenTicks) + ")");
    }
    std::printf (g_fails ? "qlink_panel_host: FAILED (%d)\n" : "qlink_panel_host: OK\n", g_fails);
    return g_fails ? 1 : 0;
}
