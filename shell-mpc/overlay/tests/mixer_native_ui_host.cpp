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
    void setNativeMixerPeekEnabled (bool enabled) noexcept override { peek.setEnabled (enabled); }
    int takeNativeMixerPeek() noexcept override { return peek.take(); }
    void setNativeMixerPeekView (int view) noexcept override { peekView = view; }
    void setNativeMixerActiveTab (int tab) noexcept override { activeTab = tab; }
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
    nativeui::MixerPeekRequest peek;
    int peekView = nativeui::MixerBase;
    int activeTab = 0;
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
    if (! mix.hostBackground() && ! (mix.hostConfirmed() && mix.peekRoot()))
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

static void assertNativeRectsTransparent (const MixerUi& mix, const std::vector<uint32_t>& pixels)
{
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
    assert (r.ui.peekRoot() == nullptr);

    const auto pixels = renderUi (r.ui, wide);
    assertNativeRectsTransparent (r.ui, pixels);
    for (int strip : { 4, 6, 8 })
    {
        const auto bounds = r.ui.screen().strips[strip].rect();
        const auto divider = inspectRect (pixels, { bounds.right() + 1, cy (bounds), 2, 1 });
        assert (divider.total == 2 && divider.transparent == divider.total);
    }
    // In host-only mode, the normal Mixer tab overlay scene is completely empty
    assert (inspectRect (pixels, r.ui.screen().routingBar.rect()).nonZero == 0);

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

    for (int i = 0; i < 5; ++i)
    {
        const auto rect = r.ui.screen().routingBar.peekRect (i);
        assert (! r.ui.covers (cx (rect), cy (rect)));
        const auto caption = inspectRect (pixels, { rect.x, rect.y, rect.w, 28 });
        assert (caption.transparent == caption.total);
    }

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

static void assertFallbackPeekIsolation()
{
    for (int peekIdx = 0; peekIdx < 5; ++peekIdx)
    {
        MixerRig r;
        seedActiveMixer (r);
        r.ui.setNativeWidgetsEnabled (true);
        r.ui.setHostBackground (true);
        r.refresh (2);

        const auto peekBtn = r.ui.screen().routingBar.peekRect (peekIdx);
        assert (! r.ui.covers (cx (peekBtn), cy (peekBtn)));
        r.inst.peek.post (float (peekIdx + 1) / 5.0f);
        r.refresh();

        assert (r.ui.peekRoot() != nullptr);
        assert (! r.ui.hostBackground());

        r.ui.setHostBackground (false);   // the opaque release fallback keeps the original editor and layout
        r.refresh();

        // Modal PEEK scene covers every pixel of the native controls and covers() returns true.
        const auto peekPixels = renderUi (r.ui, true);
        assertNativeRectsOpaque (r.ui, peekPixels);

        for (const auto rect : nativeControlRects (r.ui))
            assert (r.ui.covers (cx (rect), cy (rect)));
        assert (! r.ui.covers (10, 50));
        assert (! r.ui.covers (10, 760));

        // Direct touch, drag, and wheel inside peekPanelRect() over Bus 1 & Bus 2 native fader coords
        // must be absorbed by the PEEK modal and never leak to Mixer strips or bus params.
        const int groupBefore = r.ui.screen().activeGroup();
        const float bus1LevelBefore = r.inst.get ("bus1.level");
        const float bus2LevelBefore = r.inst.get ("bus2.level");
        const float bus1BypassBefore = r.inst.get ("bus1.fx_bypass");
        const float bus2BypassBefore = r.inst.get ("bus2.fx_bypass");

        const auto& bus1 = r.ui.screen().strips[7];
        const auto& bus2 = r.ui.screen().strips[8];
        const auto& main = r.ui.screen().strips[9];
        for (const auto rect : { bus1.power.rect(), bus1.destButton.rect(), bus1.fader.rect(),
                                 bus2.power.rect(), bus2.destButton.rect(), bus2.fader.rect(), main.power.rect() })
        {
            const int testY = cy (rect) < r.ui.peekPanelRect().bottom()
                                  ? cy (rect)
                                  : r.ui.peekPanelRect().bottom() - 20;
            const bool wheelHandled = r.ui.wheel (cx (rect), testY, 1, r.stack, r.inst, 500.0);
            assert (wheelHandled);
            r.drag (cx (rect), testY, cx (rect), testY - 30, true);
            assert (r.ui.peekRoot() != nullptr);
        }
        assert (r.ui.screen().activeGroup() == groupBefore);
        assert (r.inst.get ("bus1.level") == bus1LevelBefore);
        assert (r.inst.get ("bus2.level") == bus2LevelBefore);
        assert (r.inst.get ("bus1.fx_bypass") == bus1BypassBefore);
        assert (r.inst.get ("bus2.fx_bypass") == bus2BypassBefore);

        // Backdrop drag below peekPanelRect() directly over Bus 1 native fader bottom (956, 725)
        // must keep PEEK open and not leak touch/wheel to bus1.level.
        assert (r.ui.covers (956, 725));
        assert (r.ui.wheel (956, 725, 1, r.stack, r.inst, 600.0));
        const int writesPreDrag = r.inst.writes;
        r.drag (940, 725, 980, 725, true);
        assert (r.ui.peekRoot() != nullptr);
        assert (r.inst.writes == writesPreDrag);
        assert (r.inst.get ("bus1.level") == bus1LevelBefore);

        // The host owns the Filter model menu. The overlay stays on PEEK and isolates the Mixer strips.
        if (peekIdx == 0 || peekIdx == 1)
        {
            auto* fltFull = dynamic_cast<ui::FilterFullScreen*> (r.ui.peekRoot());
            assert (fltFull != nullptr);
            r.tap (fltFull->model);
            r.refresh (1);
            assert (r.ui.topPage() == MixerUi::kPagePeek);
            assert (r.ui.covers (956, 520));
            assert (r.ui.covers (1083, 520));
            assert (r.ui.wheel (956, 725, 1, r.stack, r.inst, 700.0));
            r.drag (956, 520, 956, 460, true);
            assert (r.inst.get ("bus1.level") == bus1LevelBefore);
            assert (r.inst.get ("bus2.level") == bus2LevelBefore);
            assert (r.ui.topPage() == MixerUi::kPagePeek);
        }
        else
        {
            auto* fxFull = dynamic_cast<ui::FxFullScreen*> (r.ui.peekRoot());
            assert (fxFull != nullptr);
            assert (r.ui.covers (956, 520));
            assert (r.ui.covers (1083, 520));
            // If dragging across chainBar opened FxUi's nested list (kPageList), verify its
            // isolation over native Bus 1/2 rects and close it back to kPagePeek before closing PEEK.
            if (r.ui.topPage() != MixerUi::kPagePeek)
            {
                assert (r.ui.wheel (956, 725, 1, r.stack, r.inst, 700.0));
                r.drag (956, 520, 956, 460, true);
                assert (r.inst.get ("bus1.level") == bus1LevelBefore);
                assert (r.inst.get ("bus2.level") == bus2LevelBefore);
                r.tapAt (10, 725, 40);
                assert (r.ui.topPage() == MixerUi::kPagePeek);
            }
        }

        // Backdrop tap at Bus 1 native fader bottom (956, 725) dismisses PEEK cleanly without writing bus1.level
        // and restores hostBackground() + transparent native rects on base page.
        const int wPreClose = r.inst.writes;
        if (r.ui.peekRoot() != nullptr)
            r.tapAt (956, 725, 40);
        r.refresh (1);
        assert (r.ui.peekRoot() == nullptr);
        assert (r.inst.writes == wPreClose);
        assert (r.inst.get ("bus1.level") == bus1LevelBefore);
        r.ui.setHostBackground (true);
        r.refresh();
        assert (r.ui.hostBackground());
        const auto afterPixels = renderUi (r.ui, true);
        assertNativeRectsTransparent (r.ui, afterPixels);
        assert (! r.ui.covers (956, 520));
    }
}

static void assertNativePeekApertures()
{
    for (int target = 0; target < 5; ++target)
    {
        MixerRig r;
        seedActiveMixer (r);
        r.inst.set ("filter1.on", 1.0f);
        r.inst.set ("filter2.on", 1.0f);
        r.inst.set ("delay.on", 1.0f);
        r.inst.set ("delay.rack", 0.0f);
        r.inst.set ("chorus.on", 1.0f);
        r.inst.set ("chorus.rack", 1.0f);
        r.inst.set ("reverb.on", 1.0f);
        r.inst.set ("reverb.rack", 2.0f);
        r.ui.setHostBackground (true);
        r.refresh();
        const int reveals = r.inst.nativeMixerReveals;
        r.inst.peek.post (static_cast<float> (target + 1) / 5);
        r.refresh();
        assert (r.ui.topPage() == MixerUi::kPagePeek);
        r.refresh();
        assert (r.inst.nativeMixerReveals == reveals);

        const auto close = r.ui.peekCloseRect(), panel = r.ui.peekPanelRect();
        assert (close.w >= 56 && close.h >= 56 && panel.contains (cx (close), cy (close)));
        const auto pixels = renderUi (r.ui, true);
        auto verify = [&] (ui::Rect area, bool native) {
            const int x = cx (area), y = cy (area);
            assert (panel.contains (x, y) && ! close.contains (x, y));
            assert (r.ui.covers (x, y) != native);
            const auto centre = inspectRect (pixels, { x, y, 1, 1 });
            assert (centre.transparent == (native ? 1 : 0));
        };
        assert (r.ui.covers (cx (close), cy (close))
                && inspectRect (pixels, { cx (close), cy (close), 1, 1 }).nonZero == 1);
        if (target < 2)
        {
            auto* filter = dynamic_cast<ui::FilterFullScreen*> (r.ui.peekRoot());
            assert (filter && filter->hasNativeControl (nativeui::FilterCutoff));
            ui::FilterFullScreen reference;
            reference.setPageMode (true);
            reference.place ({ 0, MixerUi::kTabY, MixerUi::kCanvasW, MixerUi::kEditorH });
            assert (filter->model.rect().x == reference.model.rect().x
                    && filter->cutoff.rect().y == reference.cutoff.rect().y);
            verify (filter->power.rect(), true);
            verify (filter->cutoff.rect(), true);
            verify (filter->displayRect(), false);
            const auto cutoff = filter->cutoff.rect();
            r.inst.set (target == 0 ? "filter1.on" : "filter2.on", 0.0f);
            r.refresh();
            const auto off = renderUi (r.ui, true);
            assert (r.ui.covers (cx (cutoff), cy (cutoff))
                    && inspectRect (off, { cx (cutoff), cy (cutoff), 1, 1 }).transparent == 1);
        }
        else
        {
            auto* fx = dynamic_cast<ui::FxFullScreen*> (r.ui.peekRoot());
            assert (fx && fx->nativeControls() && fx->editorShown());
            ui::FxFullScreen reference;
            reference.setNativeControls (true);
            reference.place ({ 0, MixerUi::kTabY, MixerUi::kCanvasW, MixerUi::kTabH });
            assert (fx->chainBar.nativePowerRect (0).x == reference.chainBar.nativePowerRect (0).x
                    && fx->slotEditor.power.rect().y == reference.slotEditor.power.rect().y);
            verify (fx->chainBar.nativePowerRect (0), true);
            verify (fx->chainBar.nativeSelectRect (0), true);
            verify (fx->slotEditor.power.rect(), true);
            int cells = 0;
            fx->slotEditor.visitNativeControls ([&] (ui::Rect area) { verify (area, true); ++cells; });
            assert (cells > 4);
            r.tapAt (cx (close), cy (close));
            assert (! r.ui.peekRoot());
        }
    }
}

static void assertPeekRequestsFollowThePage()
{
    MixerRig r;
    seedActiveMixer (r);
    r.ui.setHostBackground (true);
    r.inst.peek.post (0.2f);
    r.refresh();
    assert (r.inst.peek.enabled() && ! r.ui.peekRoot());
    r.inst.peek.post (0.0f);
    r.refresh();
    assert (! r.ui.peekRoot());
    r.inst.peek.post (0.2f);
    r.refresh();
    assert (dynamic_cast<ui::FilterFullScreen*> (r.ui.peekRoot()) && r.inst.peek.enabled() && r.inst.fullOpen);

    r.inst.peek.post (1.0f);
    r.stack.back();
    r.refresh (2);
    assert (! r.ui.peekRoot() && r.inst.peek.enabled());
    r.inst.peek.post (0.2f);
    r.refresh();
    assert (dynamic_cast<ui::FilterFullScreen*> (r.ui.peekRoot()));
    r.stack.back();
    r.refresh();
    r.inst.peek.post (0.6f);
    r.ui.leftTab (r.inst);
    assert (! r.inst.peek.enabled());
    assert (r.stack.closeAll());
    assert (r.stack.openFull (MixerUi::kElementPage));
    r.refresh (2);
    assert (! r.ui.peekRoot() && r.inst.peek.enabled());
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

static void assertFilterPeekNativeModulations()
{
    namespace n = nativeui::targets;
    constexpr int kMixerFrame = n::frame (n::Mixer);
    for (int target = 0; target < 2; ++target)
    {
        MixerRig r;
        seedActiveMixer (r);
        r.inst.setTabView (IInstanceView::kTabViewFilter, 3);
        const char* const cutoffKey = target == 0 ? "filter1.cutoff" : "filter2.cutoff";
        const char* const onKey = target == 0 ? "filter1.on" : "filter2.on";
        const int cutoffParam = demo::Inst::idx (cutoffKey);
        for (int i = 0; i < 8; ++i)
            r.inst.mod (i + 1, i + 1, cutoffKey, 10.0f + static_cast<float> (i));

        r.ui.setHostBackground (true);
        r.refresh();
        r.inst.peek.post (static_cast<float> (target + 1) / 5.0f);
        r.refresh();
        assert (r.ui.topPage() == MixerUi::kPagePeek);
        assert (r.inst.peekView == nativeui::MixerPeekFilter);
        assert (r.inst.filter.layout() == target + 1);
        assert (r.inst.modTargets.layout() == 0);

        auto* filter = dynamic_cast<ui::FilterFullScreen*> (r.ui.peekRoot());
        assert (filter != nullptr);
        const auto cutoffRect = filter->cutoff.rect();
        const int writesBeforeHold = r.inst.writes;
        assert (! r.ui.touch (makeTouch (TouchEvent::Down, cx (cutoffRect), cy (cutoffRect)), r.stack, r.inst, r.now));
        r.ui.tick (r.now + 600.0, r.stack, r.inst);
        r.refresh();
        assert (! r.ui.touch (makeTouch (TouchEvent::Up, cx (cutoffRect), cy (cutoffRect)), r.stack, r.inst, r.now));

        assert (r.ui.topPage() == FilterUi::kPageMods);
        assert (r.stack.depth() == 3 && r.stack.element() == MixerUi::kElementPage);
        assert (r.inst.tabView (IInstanceView::kTabViewFilter) == 3);
        assert (r.inst.peekView == nativeui::MixerBase);
        assert (r.inst.filter.layout() == 0);
        assert (r.inst.modTargets.layout() == kMixerFrame && r.inst.modTargets.mode() == n::Modulations);
        assert (r.inst.writes == writesBeforeHold);

        ui::ModulationsPage ref;
        ref.place ({ 0, MixerUi::kTabY, MixerUi::kCanvasW, MixerUi::kTabH });
        ref.setNativeControls (true);
        ui::ModInput refInput;
        for (int i = 0; i < 6; ++i)
            refInput.add (i + 1, "SRC", 0.1, false, false, false);
        ref.load (refInput, "CUTOFF", 8, 0);

        const auto pixels = renderUi (r.ui, true);
        const auto panel = r.ui.peekPanelRect();
        for (const auto area : { ref.back.rect(), ref.add.rect(), ref.page.rect(),
                                 ref.rows[0].depth.rect(), ref.rows[0].polarity.rect(),
                                 ref.rows[0].bypass.rect(), ref.rows[0].remove.rect() })
        {
            assert (panel.contains (area.x, area.y) && panel.contains (area.right() - 1, area.bottom() - 1));
            const bool mapped = area.y == ref.rows[0].depth.rect().y && area.x != ref.rows[0].remove.rect().x;
            const auto badge = mapped ? ui::QGroup::chipRect (area, 1) : ui::Rect {};
            for (int y = area.y; y < area.bottom(); ++y)
                for (int x = area.x; x < area.right(); ++x)
                    if (! badge.contains (x, y))
                        assert (!(pixels[static_cast<size_t> (y * MixerUi::kCanvasW + x)] >> 24));
            assert (! r.ui.covers (cx (area), cy (area)));
            assert (! r.ui.touch (makeTouch (TouchEvent::Down, cx (area), cy (area)), r.stack, r.inst, r.now));
            assert (! r.ui.wheel (cx (area), cy (area), 1, r.stack, r.inst, r.now));
        }
        assert (r.inst.writes == writesBeforeHold);

        for (const auto pt : { std::pair<int, int> { 2, 170 }, std::pair<int, int> { 1278, 170 },
                               std::pair<int, int> { 640, 112 }, std::pair<int, int> { 640, 736 },
                               std::pair<int, int> { 200, 170 } })
        {
            assert (r.ui.covers (pt.first, pt.second));
            assert (inspectRect (pixels, { pt.first, pt.second, 1, 1 }).nonZero == 1);
        }

        const int mod0Amount = demo::Inst::idx ("mod01.amount");
        r.inst.modTargets.post (r.inst.modTargets.at (n::field (0, n::Amount)), 0.75f, r.inst.generation);
        r.refresh();
        assert (r.inst.writes == writesBeforeHold + 1 && r.inst.lastWritten == mod0Amount);

        const float beforeStale = r.inst.get ("mod01.amount");
        const int beforePreset = r.inst.writes;
        r.inst.modTargets.post (r.inst.modTargets.at (n::field (0, n::Amount)), 0.25f, r.inst.generation);
        ++r.inst.generation;
        r.refresh();
        assert (r.inst.writes == beforePreset && r.inst.get ("mod01.amount") == beforeStale);

        r.inst.modTargets.post (r.inst.modTargets.at (n::field (0, n::Amount)), 0.15f, r.inst.generation);
        r.inst.modTargets.post (r.inst.modTargets.at (n::Back), 1.0f, r.inst.generation);
        r.refresh();
        assert (r.inst.writes == beforePreset && r.inst.get ("mod01.amount") == beforeStale);
        assert (r.ui.topPage() == MixerUi::kPagePeek && r.stack.depth() == 2 && r.stack.element() == MixerUi::kElementPage);
        assert (r.ui.peekRoot() != nullptr);
        assert (r.inst.modTargets.layout() == 0);
        assert (r.inst.peekView == nativeui::MixerPeekFilter);
        assert (r.inst.filter.layout() == target + 1 && r.inst.filter.at (nativeui::FilterCutoff).cell().target == cutoffParam);
        assert (r.inst.tabView (IInstanceView::kTabViewFilter) == 3);

        // Reopen Modulations, verify fallback switch retires native bank, and verify OFF closes and rejects stale writes.
        assert (! r.ui.touch (makeTouch (TouchEvent::Down, cx (cutoffRect), cy (cutoffRect)), r.stack, r.inst, r.now));
        r.ui.tick (r.now + 600.0, r.stack, r.inst);
        r.refresh();
        assert (! r.ui.touch (makeTouch (TouchEvent::Up, cx (cutoffRect), cy (cutoffRect)), r.stack, r.inst, r.now));
        assert (r.ui.topPage() == FilterUi::kPageMods && r.inst.modTargets.layout() == kMixerFrame);

        r.ui.setHostBackground (false);
        r.refresh();
        assert (r.inst.modTargets.layout() == 0 && r.ui.covers (cx (ref.rows[0].depth.rect()), cy (ref.rows[0].depth.rect())));
        r.ui.setHostBackground (true);
        r.refresh();
        assert (r.inst.modTargets.layout() == kMixerFrame);

        const int beforeOff = r.inst.writes;
        r.inst.modTargets.post (r.inst.modTargets.at (n::field (0, n::Amount)), 0.9f, r.inst.generation);
        r.inst.set (onKey, 0.0f);
        r.refresh();
        assert (r.ui.topPage() == MixerUi::kPagePeek && r.stack.depth() == 2);
        assert (r.inst.modTargets.layout() == 0 && r.inst.writes == beforeOff && r.inst.get ("mod01.amount") == beforeStale);
    }
}

static void assertAll36TabTransitionsAndTabBarExclusion()
{
    MixerRig r;
    seedActiveMixer (r);
    r.inst.set ("filter1.on", 1.0f);
    r.inst.set ("filter2.on", 1.0f);
    r.inst.set ("delay.on", 1.0f);
    r.inst.set ("delay.rack", 0.0f);
    r.inst.set ("chorus.on", 1.0f);
    r.inst.set ("chorus.rack", 1.0f);
    r.inst.set ("reverb.on", 1.0f);
    r.inst.set ("reverb.rack", 2.0f);
    r.inst.setTabView (IInstanceView::kTabViewFilter, 3);
    r.ui.setHostBackground (true);
    r.refresh (2);

    // Initial tab is 0 (MIXER), host-only, 0 overlay ops
    assert (r.ui.activeTab() == 0);
    assert (r.ui.peekRoot() == nullptr);
    assert (r.ui.hostBackground());

    const ui::Rect tabBar { 0, MixerUi::kTabY + 566, MixerUi::kCanvasW, 56 };
    for (int from = 0; from < 6; ++from)
    {
        for (int to = 0; to < 6; ++to)
        {
            // Navigate to `from`
            r.inst.peek.post (static_cast<float> (from) / 5.0f);
            r.refresh();
            assert (r.ui.activeTab() == from);

            // Transition `from -> to`
            r.inst.peek.post (static_cast<float> (to) / 5.0f);
            r.refresh();
            assert (r.ui.activeTab() == to);

            const auto pixels = renderUi (r.ui, true);
            // Persistent bottom 6-tab bar must have ZERO overlay pixels and ZERO overlay input capture in EVERY view
            assert (inspectRect (pixels, tabBar).nonZero == 0);
            for (int k = 0; k < 6; ++k)
            {
                const int bx = 10 + 210 * k + 105;
                const int by = MixerUi::kTabY + 566 + 28;
                assert (! r.ui.covers (bx, by));
            }

            if (to == 0)
            {
                assert (r.ui.peekRoot() == nullptr);
                assert (r.ui.hostBackground());
                assert (r.inst.peekView == nativeui::MixerBase);
                assertNativeRectsTransparent (r.ui, pixels);
            }
            else if (to == 1 || to == 2)
            {
                auto* flt = dynamic_cast<ui::FilterFullScreen*> (r.ui.peekRoot());
                assert (flt != nullptr);
                assert (r.inst.peekView == nativeui::MixerPeekFilter);
                assert (r.inst.filter.layout() == to);
                assert (flt->rect().bottom() <= MixerUi::kTabY + 564);
            }
            else
            {
                auto* fx = dynamic_cast<ui::FxFullScreen*> (r.ui.peekRoot());
                assert (fx != nullptr);
                assert (r.inst.peekView == nativeui::MixerPeekFx);
                assert (fx->rect().bottom() <= MixerUi::kTabY + 564);
                const int expectedType = to == 3 ? synthyboy::kFxTypeDelay : to == 4 ? synthyboy::kFxTypeChorus : synthyboy::kFxTypeReverb;
                assert (fx->chainBar.numActiveSlots() >= 1 && fx->slotEditor.currentType() == expectedType);
            }
            assert (r.inst.tabView (IInstanceView::kTabViewFilter) == 3);
        }
    }
    // Return to MIXER cleanly
    r.inst.peek.post (0.0f);
    r.refresh();
    assert (r.ui.activeTab() == 0 && r.ui.peekRoot() == nullptr);
}

} // namespace

int main()
{
    assertBasePageNativeHandoff (true);
    assertBasePageNativeHandoff (false);
    assertOffSourcesAndDryBypass();
    assertMeterDisplayValues();
    assertFallbackPeekIsolation();
    assertNativePeekApertures();
    assertPeekRequestsFollowThePage();
    assertDestinationPopupIsolation();
    assertFilterPeekNativeModulations();
    assertAll36TabTransitionsAndTabBarExclusion();
    std::printf ("mixer_native_ui_host: all assertions passed\n");
    return 0;
}
