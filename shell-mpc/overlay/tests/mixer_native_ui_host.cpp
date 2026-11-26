// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "../filter_ui.h"
#include "../fx_ui.h"
#include "../mixer_ui.h"
#include "demo_common.h"
#include "synthyboy/params.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace synthyboy_mpc::overlay;
namespace ui = synthyboy_mpc::ui;
namespace nativeui = synthyboy_mpc::nativeui;

namespace
{

class MixerInstance final : public demo::Inst
{
public:
    void setNativeMixerActive (bool active) noexcept override { mixerActive = active; }
    void setNativeMixerStripVisuals (int strip, int soloV, int muteV) noexcept override
    {
        soloVisual[strip] = soloV;
        muteVisual[strip] = muteV;
    }
    void setNativeFilterControls (int view, const nativeui::FxCell* cells, const float*, const int*) noexcept override
    {
        filter.publish (view, cells);
    }
    bool takeNativeFilterSlot (int slot, const nativeui::FxCell& expected, float& value) noexcept override
    {
        return filter.take (slot, expected, value);
    }
    bool nativeModTargetsSupported() const noexcept override { return true; }
    void publishNativeModTargets (const nativeui::targets::State& state) noexcept override
    {
        modTargets.publish (state);
        for (int slot = 0; slot < nativeui::targets::kFields; ++slot) modTargets.report (modTargets.at (slot));
    }
    void hideNativeModTargets (const void* presenter) noexcept override { modTargets.hide (presenter); }
    bool takeNativeModTarget (const nativeui::targets::State& state, int slot, float& value) noexcept override
    {
        return modTargets.take (state, slot, value);
    }
    void setNativeMixerMeter (int strip, float peak, float hold) noexcept override
    {
        peaks[strip] = nativeui::meterValue (peak);
        holds[strip] = nativeui::meterValue (hold);
    }

    void setUiFullOpen (bool open) noexcept override
    {
        if (! open) ++nativeMixerReveals;
        fullOpen = open;
    }
    int nativeMixerReveals = 0;
    bool mixerActive = false;
    int soloVisual[nativeui::kMixerStrips] = {};
    int muteVisual[nativeui::kMixerStrips] = {};
    nativeui::FilterCellBank filter;
    nativeui::targets::Bank modTargets;
    float peaks[nativeui::kMixerStrips] = {}, holds[nativeui::kMixerStrips] = {};
};
using MixerRig = demo::Rig<MixerUi, MixerInstance>;

struct PixelStats
{
    int total = 0;
    int transparent = 0;
    int nonZero = 0;
};

static int cx (ui::Rect r) noexcept { return r.x + r.w / 2; }
static int cy (ui::Rect r) noexcept { return r.y + r.h / 2; }
static bool rectsIntersect (ui::Rect a, ui::Rect b) noexcept
{
    return a.x < b.right() && a.right() > b.x && a.y < b.bottom() && a.bottom() > b.y;
}

static std::vector<uint32_t> renderUi (MixerUi& mix, bool wide)
{
    ui::QGroup::setWideDevice (wide);
    auto data = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder sb (*data);
    sb.begin (gpu::kKindPlay, 0, MixerUi::kCanvasW, MixerUi::kCanvasH);
    sb.clear (0u);
    if (! mix.hostBackground())
        page::ground (sb);
    mix.draw (sb);
    std::vector<uint32_t> pixels;
    renderScene (*data, pixels);
    return pixels;
}

static PixelStats inspectRect (const std::vector<uint32_t>& pixels, ui::Rect rect)
{
    PixelStats s;
    const int x0 = rect.x < 0 ? 0 : rect.x;
    const int y0 = rect.y < 0 ? 0 : rect.y;
    const int x1 = rect.right() > MixerUi::kCanvasW ? MixerUi::kCanvasW : rect.right();
    const int y1 = rect.bottom() > MixerUi::kCanvasH ? MixerUi::kCanvasH : rect.bottom();
    for (int y = y0; y < y1; ++y)
    {
        for (int x = x0; x < x1; ++x)
        {
            const uint32_t px = pixels[static_cast<size_t> (y * MixerUi::kCanvasW + x)];
            ++s.total;
            const uint8_t a = static_cast<uint8_t> (px >> 24);
            if (a == 0)
                ++s.transparent;
            else
                ++s.nonZero;
        }
    }
    return s;
}

static std::vector<ui::Rect> nativeControlRects (const MixerUi& mix)
{
    std::vector<ui::Rect> rects;
    for (const auto& strip : mix.screen().strips)
    {
        rects.push_back (strip.power.rect());
        rects.push_back (strip.fader.rect());
        if (strip.hasDestination()) rects.push_back (strip.destButton.rect());
        if (strip.hasPan()) rects.push_back (strip.pan.rect());
        if (strip.hasSend1()) rects.push_back (strip.send1.rect());
        if (strip.hasSend2()) rects.push_back (strip.send2.rect());
    }
    assert (rects.size() == 50);
    return rects;
}

static void assertNativeRectsTransparent (const MixerUi& mix, std::vector<uint32_t> pixels)
{
    // The only foreground paint permitted inside the root's native controls is
    // the opaque cover over each host identification marker (one-pixel margin).
    for (const auto& view : layout::kViews)
        if (std::strcmp (view.name, "mixer") == 0)
            for (const auto spot : { view.markers[0].rect, view.instance, view.markers[1].rect })
                for (int y = spot.y - 1; y <= spot.y + spot.h; ++y)
                    for (int x = spot.x - 1; x <= spot.x + spot.w; ++x)
                    {
                        auto& pixel = pixels[static_cast<size_t> (y * MixerUi::kCanvasW + x)];
                        assert ((pixel >> 24) == 255u);
                        pixel = 0;
                    }
    for (auto rect : nativeControlRects (mix))
    {
        if (rect.h > 56)
        {
            // The Q-Link frame and badge can share the outer edge of the fader.
            rect = { rect.x + 8, rect.y + 8, rect.w - 16, rect.h - 16 };
        }
        const auto stats = inspectRect (pixels, rect);
        if (stats.nonZero > 0)
            std::fprintf (stderr, "native aperture %d,%d %dx%d has %d overlay pixels\n",
                          rect.x, rect.y, rect.w, rect.h, stats.nonZero);
        assert (stats.total > 0 && stats.transparent == stats.total);
    }
}

static void assertNativeRectsOpaque (const MixerUi& mix, const std::vector<uint32_t>& pixels)
{
    for (const auto rect : nativeControlRects (mix))
    {
        const auto stats = inspectRect (pixels, rect);
        assert (stats.total > 0 && stats.transparent == 0);
    }
}

static TouchEvent makeTouch (TouchEvent::Kind kind, int x, int y)
{
    TouchEvent e;
    e.kind = kind;
    e.x = x;
    e.y = y;
    return e;
}

static void seedActiveMixer (MixerRig& r)
{
    r.inst.set ("sub.on", 1.0f);
    r.inst.set ("sub.level", 0.5f);
    r.inst.set ("osc1.on", 1.0f);
    r.inst.set ("osc1.level", 0.5f);
    r.inst.set ("osc2.on", 1.0f);
    r.inst.set ("osc3.on", 1.0f);
    r.inst.set ("noise.on", 1.0f);
    r.inst.set ("noise.level", 0.5f);
    r.inst.set ("filter1.on", 1.0f);
    r.inst.set ("filter2.on", 1.0f);
    r.inst.set ("bus1.fx_bypass", 0.0f);
    r.inst.set ("bus2.fx_bypass", 0.0f);
    r.inst.set ("bus1.level", 0.35f);
    r.inst.set ("bus2.level", 0.45f);
}

static void assertBasePageNativeHandoff (bool wide)
{
    MixerRig r;
    seedActiveMixer (r);
    r.ui.setNativeWidgetsEnabled (true);
    r.ui.setHostBackground (true);
    r.refresh (2);

    assert (r.ui.hostBackground());
    assert (r.ui.nativeWidgetsEnabled());
    assert (! r.ui.listOpen());
    assert (r.inst.mixerActive);

    const auto pixels = renderUi (r.ui, wide);
    assertNativeRectsTransparent (r.ui, pixels);
    for (int strip : { 4, 6, 8 })
    {
        const auto bounds = r.ui.screen().strips[strip].rect();
        const auto divider = inspectRect (pixels, { bounds.right() + 1, cy (bounds), 2, 1 });
        assert (divider.total == 2 && divider.transparent == divider.total);
    }
    // In host-only mode, the normal Mixer tab overlay scene is completely empty
    const auto strips = r.ui.screen().strips[9].rect();
    assert (inspectRect (pixels, { strips.x, strips.bottom(), strips.w, 12 }).nonZero == 0);

    const int writesBefore = r.inst.writes;
    for (const auto rect : nativeControlRects (r.ui))
    {
        assert (! r.ui.covers (cx (rect), cy (rect)));
        assert (! r.ui.touch (makeTouch (TouchEvent::Down, cx (rect), cy (rect)), r.stack, r.inst, 100.0));
        assert (! r.ui.fingerDown());
        assert (! r.ui.wheel (cx (rect), cy (rect), 1, r.stack, r.inst, 110.0));
    }
    assert (r.inst.writes == writesBefore);
    assert (r.ui.screen().selectedStrip() == 9);

    // The strips fill the whole tab: the freed bottom row (the old peek tab bar) belongs to them
    assert (r.ui.screen().strips[9].rect().bottom() == r.ui.screen().rect().bottom() - 12);

    r.ui.setHostBackground (false);
    assertNativeRectsOpaque (r.ui, renderUi (r.ui, wide));
    for (const auto rect : nativeControlRects (r.ui))
        assert (r.ui.covers (cx (rect), cy (rect)));
    const auto sub = r.ui.screen().strips[0].fader.rect();
    r.drag (cx (sub), cy (sub), cx (sub), sub.y + 10, true);
    assert (r.inst.writes > writesBefore);
    assert (r.ui.screen().selectedStrip() == 0 && r.ui.screen().activeGroup() == 0);
}

static void assertOffSourcesAndDryBypass()
{
    MixerRig r;
    seedActiveMixer (r);
    const char* const offSources[] = { "sub.on", "osc1.on", "osc2.on", "osc3.on", "noise.on", "filter1.on", "filter2.on" };
    for (const char* power : offSources)
        r.inst.set (power, 0.0f);
    r.inst.set ("bus1.fx_bypass", 1.0f);
    r.inst.set ("bus2.fx_bypass", 1.0f);
    r.inst.set ("main.fx_bypass", 1.0f);
    r.inst.haveLive = true;
    for (int i = 0; i < nativeui::kMixerStrips; ++i)
        r.inst.liveValues.stripPeak[i] = 0.1f * i;
    r.ui.setHostBackground (true);
    r.refresh();
    const auto pixels = renderUi (r.ui, false);
    const int before = r.inst.writes;
    for (int i = 0; i < 7; ++i)
    {
        auto& strip = r.ui.screen().strips[i];
        assert (! r.ui.covers (cx (strip.power.rect()), cy (strip.power.rect())));
        for (auto* field : { static_cast<ui::Component*> (&strip.fader), static_cast<ui::Component*> (&strip.pan),
                             static_cast<ui::Component*> (&strip.send1), static_cast<ui::Component*> (&strip.send2),
                             static_cast<ui::Component*> (&strip.destButton) })
        {
            const auto rect = field->rect();
            assert (field->inactive() && ! field->enabled());
            // Normal Mixer emits zero own EGL ops anywhere: overlay pixels are completely empty
            assert (inspectRect (pixels, rect).nonZero == 0);
            assert (! r.ui.covers (cx (rect), cy (rect)));
            assert (! r.ui.wheel (cx (rect), cy (rect), 1, r.stack, r.inst, r.now));
            r.drag (cx (rect), cy (rect), cx (rect), rect.y + 5, true);
            assert (! r.ui.listOpen());
        }
        const int slot = ui::MixerFullScreen::slotOfStrip (i);
        const int group = ui::MixerFullScreen::groupOfStrip (i);
        assert (r.inst.q[slot] == -1 && r.ui.screen().groups[group].member (slot) == nullptr);
        r.ui.turnQ (slot, 0.1, r.stack, r.inst);
    }
    assert (r.inst.writes == before);

    for (int i = 7; i < 10; ++i)
    {
        auto& strip = r.ui.screen().strips[i];
        assert (! strip.power.state.on && strip.fader.enabled() && ! strip.fader.inactive());
        const auto level = strip.fader.rect();
        const ui::Rect levelBody {level.x + 8, level.y + 8, level.w - 16, level.h - 16};
        assert (inspectRect (pixels, levelBody).nonZero == 0 && ! r.ui.covers (cx (level), cy (level)));
        if (strip.hasDestination())
        {
            const auto destination = strip.destButton.rect();
            assert (strip.destButton.enabled() && ! strip.destButton.inactive());
            assert (inspectRect (pixels, destination).nonZero == 0 && ! r.ui.covers (cx (destination), cy (destination)));
        }
        assert (r.inst.peaks[i] == r.inst.liveValues.stripPeak[i]);
        r.ui.touch (makeTouch (TouchEvent::Down, cx (level), cy (level)), r.stack, r.inst, r.now);
        const int slot = ui::MixerFullScreen::slotOfStrip (i);
        assert (r.inst.q[slot] == r.ui.binding (i).levelParam);
        r.ui.turnQ (slot, -0.1, r.stack, r.inst);
    }
    assert (r.inst.writes == before + 3);
}

static void assertMeterDisplayValues()
{
    MixerRig r;
    seedActiveMixer (r);
    r.ui.setHostBackground (true);
    r.inst.haveLive = true;
    for (int i = 0; i < nativeui::kMixerStrips; ++i)
        r.inst.liveValues.stripPeak[i] = float (i + 1) / 11.0f;
    r.refresh();
    for (int i = 0; i < nativeui::kMixerStrips; ++i)
        assert (r.inst.peaks[i] == r.inst.liveValues.stripPeak[i] && r.inst.holds[i] == r.inst.peaks[i]);
    r.inst.liveValues.stripPeak[0] = -0.1f;
    r.inst.liveValues.stripPeak[1] = 1.1f;
    r.inst.liveValues.stripPeak[2] = std::numeric_limits<float>::quiet_NaN();
    r.inst.liveValues.stripPeak[3] = std::numeric_limits<float>::infinity();
    r.refresh();
    assert (r.inst.peaks[0] == 0.0f && r.inst.peaks[1] == 1.0f && r.inst.holds[1] == 1.0f);
    assert (r.inst.peaks[2] == 0.0f && r.inst.peaks[3] == 0.0f && r.inst.holds[2] > 0.0f);
    r.refresh (10);
    assert (r.inst.holds[2] == 0.0f && r.inst.holds[3] == 0.0f);
    assert (r.inst.writes == 0);
    assert (nativeui::meterValue (std::numeric_limits<float>::quiet_NaN()) == 0.0f);
}

static void assertMixerActiveFollowsThePage()
{
    MixerRig r;
    seedActiveMixer (r);
    r.ui.setHostBackground (true);
    r.refresh (2);
    assert (r.inst.mixerActive && r.inst.fullOpen == true);
    r.ui.leftTab (r.inst);
    assert (! r.inst.mixerActive);
    assert (r.stack.closeAll());
    assert (r.stack.openFull (MixerUi::kElementPage));
    r.refresh (2);
    assert (r.inst.mixerActive);
    assert (r.inst.writes == 0);
}

static void assertDestinationPopupIsolation()
{
    // Part A: Open the overlay fallback destination popup before native controls become available.
    {
        MixerRig r;
        seedActiveMixer (r);
        r.ui.setNativeWidgetsEnabled (false);
        r.ui.setHostBackground (true);
        r.refresh (2);

        const auto noiseDest = r.ui.screen().strips[4].destButton.rect();
        r.tapAt (cx (noiseDest), cy (noiseDest), 40);
        r.refresh (1);
        assert (r.ui.listOpen());
        r.ui.setNativeWidgetsEnabled (true);

        // Every native control must be covered and obscured (scrim + list box).
        for (const auto rect : nativeControlRects (r.ui))
            assert (r.ui.covers (cx (rect), cy (rect)));
        assert (! r.ui.covers (10, 50));
        assert (! r.ui.covers (10, 760));

        const auto popupPixels = renderUi (r.ui, true);
        assertNativeRectsOpaque (r.ui, popupPixels);

        // Backdrop wheel directly over Bus 1 native fader / Bus 2 native fader / F1 dest must be consumed with 0 writes.
        const int w0 = r.inst.writes;
        const int groupBefore = r.ui.screen().activeGroup();
        for (const auto rect : nativeControlRects (r.ui))
        {
            if (! r.ui.list().rect().contains (cx (rect), cy (rect)))
                assert (r.ui.wheel (cx (rect), cy (rect), 1, r.stack, r.inst, 800.0));
        }
        assert (r.ui.listOpen());
        assert (r.inst.writes == w0);
        assert (r.ui.screen().activeGroup() == groupBefore);

        // Tapping dimmed backdrop over Bus 1 native fader closes the popup cleanly without leaking writes.
        r.tapAt (956, 520, 40);
        r.refresh (1);
        assert (! r.ui.listOpen());
        assert (r.inst.writes == w0);
        assert (r.ui.screen().activeGroup() == groupBefore);
        assert (! r.ui.covers (956, 520));
        assertNativeRectsTransparent (r.ui, renderUi (r.ui, true));
    }

    // Part B: Open Bus 1 destination popup directly over Bus 1 / Bus 2 native faders,
    // then ensure list rows overlapping native rects receive touches cleanly.
    {
        MixerRig r;
        seedActiveMixer (r);
        r.ui.setNativeWidgetsEnabled (false);
        r.ui.setHostBackground (true);
        r.refresh (2);

        const auto bus1Dest = r.ui.screen().strips[7].destButton.rect();
        r.tapAt (cx (bus1Dest), cy (bus1Dest), 40);
        r.refresh (1);
        assert (r.ui.listOpen());

        r.ui.setNativeWidgetsEnabled (true);
        for (const auto rect : nativeControlRects (r.ui))
            assert (r.ui.covers (cx (rect), cy (rect)));

        // List rect itself overlaps Bus 1/2 native rects; tapping row 1 commits destination & closes cleanly.
        const ui::Rect listBox = r.ui.list().rect();
        assert (rectsIntersect (listBox, r.ui.screen().strips[7].fader.rect())
             || rectsIntersect (listBox, r.ui.screen().strips[8].fader.rect()));
        const int rowTapX = cx (listBox);
        const int rowTapY = listBox.y + 40;
        const int w0 = r.inst.writes;
        r.tapAt (rowTapX, rowTapY, 40);
        r.refresh (1);
        assert (! r.ui.listOpen());
        assert (r.inst.writes > w0);
        assert (! r.ui.covers (956, 520));

        // Wheel down over the list box while open must advance from row 0 and commit cleanly rather than being blocked.
        r.ui.setNativeWidgetsEnabled (false);
        r.tapAt (cx (bus1Dest), cy (bus1Dest), 40);
        r.refresh (1);
        assert (r.ui.listOpen());
        r.ui.setNativeWidgetsEnabled (true);
        const int wWheel = r.inst.writes;
        assert (r.ui.wheel (rowTapX, rowTapY, -1, r.stack, r.inst, 850.0));
        assert (r.inst.writes > wWheel);
        assert (! r.ui.listOpen());

        // Hardware Back while finger is held on list must dropGesture() so Up makes 0 extra writes.
        r.ui.setNativeWidgetsEnabled (false);
        r.tapAt (cx (bus1Dest), cy (bus1Dest), 40);
        r.refresh (1);
        assert (r.ui.listOpen());
        r.ui.setNativeWidgetsEnabled (true);

        const int w1 = r.inst.writes;
        assert (r.ui.touch (makeTouch (TouchEvent::Down, rowTapX, rowTapY), r.stack, r.inst, 900.0));
        assert (r.ui.fingerDown());
        r.stack.back();
        r.ui.update (r.inst, r.stack, 1000.0, r.in);
        assert (! r.ui.listOpen());
        assert (! r.ui.fingerDown());
        r.ui.touch (makeTouch (TouchEvent::Up, rowTapX, rowTapY), r.stack, r.inst, 1020.0);
        assert (r.inst.writes == w1);
    }
}

} // namespace

int main()
{
    assertBasePageNativeHandoff (true);
    assertBasePageNativeHandoff (false);
    assertOffSourcesAndDryBypass();
    assertMeterDisplayValues();
    assertMixerActiveFollowsThePage();
    assertDestinationPopupIsolation();
    std::printf ("mixer_native_ui_host: all assertions passed\n");
    return 0;
}
