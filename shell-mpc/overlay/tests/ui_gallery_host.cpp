// SynthyBoy - MPC overlay: UI gallery renderer host
// Renders every plugin-drawn page of the overlay build at 1280x800 into PNGs.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "../../ui/ui.h"
#include "../../ui/tests/ui_test.h"
#include "demo_common.h"
#include "pages.h"
#include "play_view.h"
#include "synthyboy/filter_response.h"
#include "synthyboy/fx_rack.h"
#include "synthyboy/fx_types.h"
#include "synthyboy/osc_cycle.h"
#include "synthyboy/params.h"
#include "synthyboy/preset.h"
#include "synthyboy/state.h"

namespace fs = std::filesystem;
using namespace synthyboy_mpc;
using namespace synthyboy_mpc::overlay;

namespace
{

class GalleryInst final : public IInstanceView
{
public:
    GalleryInst()
    {
        const synthyboy::ParamInfo* t = synthyboy::paramTable();
        for (size_t i = 0; i < synthyboy::paramCount(); ++i)
            v_[i] = t[i].def;
        initLiveValues();
    }

    void initLiveValues()
    {
        haveLive = true;
        liveValues.stripPeak[0] = 0.5f;
        liveValues.stripPeak[1] = 0.72f;
        liveValues.stripPeak[2] = 0.64f;
        liveValues.stripPeak[5] = 0.83f;
        liveValues.stripPeak[6] = 0.5f;
        liveValues.stripPeak[7] = 0.3f;
        liveValues.stripPeak[9] = 0.78f;
        liveValues.activeVoices = 4;
        for (float& s : liveValues.voiceStep)
            s = -1.0f;
        liveValues.voiceStep[0] = 2.0f;
        liveValues.voiceStep[1] = 3.0f;
        for (int i = 0; i < synthyboy::LiveValues::kNumEnvs; ++i)
        {
            liveValues.envFineStage[i] = 1;
            liveValues.envFineFraction[i] = 0.35f;
        }
        for (int i = 0; i < synthyboy::LiveValues::kNumLfos; ++i)
            liveValues.lfoPhase[i] = 0.4f;
        liveValues.source[0] = 0.7f;
        liveValues.source[6] = 0.6f;
        liveValues.source[7] = -0.3f;
    }

    float param (int i) const noexcept override
    {
        return (i >= 0 && static_cast<size_t> (i) < synthyboy::kMaxParams) ? v_[i] : 0.0f;
    }

    bool live (synthyboy::LiveValues& out) const noexcept override
    {
        out = liveValues;
        return haveLive;
    }

    double sampleRate() const noexcept override { return 48000.0; }
    double bpm() const noexcept override { return 120.0; }

    void writeParam (int i, float norm) noexcept override
    {
        if (i >= 0 && static_cast<size_t> (i) < synthyboy::kMaxParams)
        {
            const synthyboy::ParamInfo& p = synthyboy::paramTable()[i];
            v_[i] = synthyboy::constrainValue (p, synthyboy::fromNormalised (p, norm));
        }
    }

    void setQGroup (const int* t, int n) noexcept override
    {
        for (int i = 0; i < 4; ++i)
            q[i] = i < n ? t[i] : -1;
    }

    void setUiFullOpen (bool open) noexcept override { fullOpen = open; }

    int playSelection (int group) const noexcept override
    {
        return (group >= 0 && group < 3) ? playSel[group] : 1;
    }

    void setPlaySelection (int group, int inst) noexcept override
    {
        if (group >= 0 && group < 3)
            playSel[group] = inst;
    }

    int playLastSelector() const noexcept override { return 2; }

    std::shared_ptr<const synthyboy::StateContent> content() const override { return content_; }

    static int idx (const std::string& key)
    {
        const auto* p = synthyboy::findParamByKey (key.c_str());
        return p ? static_cast<int> (p - synthyboy::paramTable()) : -1;
    }

    float get (const std::string& key) const
    {
        const int i = idx (key);
        return i >= 0 ? v_[i] : 0.0f;
    }

    void set (const std::string& key, float v)
    {
        const int i = idx (key);
        if (i >= 0)
            v_[i] = v;
    }

    void setAt (int i, float v)
    {
        if (i >= 0 && static_cast<size_t> (i) < synthyboy::kMaxParams)
            v_[i] = v;
    }

    void mod (int n, int source, const std::string& destKey, float amount, bool bipolar = false, bool bypass = false)
    {
        char k[24];
        std::snprintf (k, sizeof k, "mod%02d.", n);
        set (std::string (k) + "source", static_cast<float> (source));
        set (std::string (k) + "destination", static_cast<float> (synthyboy::modDestinationChoiceOf (static_cast<size_t> (idx (destKey)))));
        set (std::string (k) + "amount", amount);
        set (std::string (k) + "bipolar", bipolar ? 1.0f : 0.0f);
        set (std::string (k) + "bypass", bypass ? 1.0f : 0.0f);
    }

    bool loadPresetFile (const std::string& path)
    {
        std::ifstream f (path, std::ios::binary);
        if (! f.is_open())
            return false;
        std::string str ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
        synthyboy::PresetMeta meta;
        synthyboy::ParamValues values;
        synthyboy::StateContent content;
        synthyboy::LoadResult res = synthyboy::loadPreset (str.data(), str.size(), meta, values, content);
        if (res.status == synthyboy::LoadStatus::invalid)
            return false;
        for (size_t i = 0; i < synthyboy::paramCount(); ++i)
            v_[i] = values.plain[i];
        if (res.content == synthyboy::ContentStatus::ok)
            content_ = std::make_shared<synthyboy::StateContent> (std::move (content));
        return true;
    }

    int q[4] = { -1, -1, -1, -1 };
    int playSel[3] = { 2, 1, 1 };
    bool fullOpen = false;
    bool haveLive = true;
    synthyboy::LiveValues liveValues;

private:
    float v_[synthyboy::kMaxParams] = {};
    std::shared_ptr<const synthyboy::StateContent> content_;
};

inline uint32_t lerpRgb (uint32_t c0, uint32_t c1, float t)
{
    const int r0 = (c0 >> 16) & 255, g0 = (c0 >> 8) & 255, b0 = c0 & 255;
    const int r1 = (c1 >> 16) & 255, g1 = (c1 >> 8) & 255, b1 = c1 & 255;
    const int r = std::clamp (static_cast<int> (std::round (r0 + (r1 - r0) * t)), 0, 255);
    const int g = std::clamp (static_cast<int> (std::round (g0 + (g1 - g0) * t)), 0, 255);
    const int b = std::clamp (static_cast<int> (std::round (b0 + (b1 - b0) * t)), 0, 255);
    return ui::theme::rgba (static_cast<uint32_t> ((r << 16) | (g << 8) | b), 255);
}

void compositeAndWritePng (const std::string& path, uit::Image& im)
{
    const ui::theme::Look& T = ui::theme::L();
    const int w = im.w, h = im.h;
    for (int y = 0; y < h; ++y)
    {
        const float t = static_cast<float> (y) / static_cast<float> (h);
        const uint32_t bg = lerpRgb (T.bg0, T.bg1, t);
        for (int x = 0; x < w; ++x)
        {
            uint32_t& p = im.px[static_cast<size_t> (y) * static_cast<size_t> (w) + static_cast<size_t> (x)];
            const uint32_t a = p >> 24;
            if (a == 0)
                p = bg;
            else if (a < 255)
            {
                const uint32_t inv = 255 - a;
                const uint32_t r = ((p >> 16) & 255) + (((bg >> 16) & 255) * inv) / 255;
                const uint32_t g = ((p >> 8) & 255) + (((bg >> 8) & 255) * inv) / 255;
                const uint32_t b = (p & 255) + ((bg & 255) * inv) / 255;
                p = (255 << 24) | (r << 16) | (g << 8) | b;
            }
        }
    }
    uit::writePng (path, im, 1);
}

template <class F>
void saveScene (const std::string& path, F&& draw, int w = 1280, int h = 800)
{
    auto scene = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder sb (*scene);
    sb.begin (gpu::kKindPlay, 0, w, h);
    const ui::theme::Look& T = ui::theme::L();
    sb.clear (ui::theme::rgba (T.bg0, 255));
    sb.gradientRect (0.0f, 0.0f, static_cast<float> (w), static_cast<float> (h), ui::theme::rgba (T.bg0), ui::theme::rgba (T.bg1));
    draw (sb);
    uit::Image im;
    im.w = w; im.h = h;
    synthyboy_mpc::overlay::renderScene (*scene, im.px);
    compositeAndWritePng (path, im);
}

void savePlay (const std::string& path, int highlight, GalleryInst& inst, int w = 1280, int h = 800)
{
    PlayView pv (highlight, 0.0);
    UiStack stack;
    ViewInput in;
    in.instance = &inst;
    in.io = &inst;
    in.ui = &stack;
    in.live = inst.haveLive ? &inst.liveValues : nullptr;
    in.nowMs = 1000.0;
    pv.update (in);
    in.nowMs = 1500.0;
    pv.update (in);

    uit::Image im;
    im.w = w; im.h = h;
    im.px.assign (static_cast<size_t> (w) * static_cast<size_t> (h), 0u);
    pv.render (im.px.data(), w, h);
    compositeAndWritePng (path, im);
}

template <class Ui>
struct GalleryRig
{
    Ui ui;
    GalleryInst inst;
    UiStack stack;
    page::Input in;
    double now = 1000.0;

    explicit GalleryRig (const GalleryInst& baseInst, int instance = 1)
        : inst (baseInst)
    {
        in.instance = instance;
        stack.openFull (Ui::kElementPage);
    }

    void refresh (int times = 2)
    {
        for (int i = 0; i < times; ++i)
        {
            now += 150.0;
            in.live = inst.haveLive ? &inst.liveValues : nullptr;
            ui.update (inst, stack, now, in);
        }
    }

    void picture (const std::string& path)
    {
        saveScene (path, [this] (gpu::SceneBuilder& sb) { ui.draw (sb); });
    }

    void tapAt (int x, int y, int holdMs = 40)
    {
        TouchEvent e; e.kind = TouchEvent::Down; e.x = x; e.y = y;
        ui.touch (e, stack, inst, now);
        ui.tick (now + holdMs, stack, inst);
        e.kind = TouchEvent::Up;
        ui.touch (e, stack, inst, now + holdMs);
        refresh();
    }
    void tap (const ui::Component& c, int holdMs = 40)
    {
        tapAt (c.rect().x + c.rect().w / 2, c.rect().y + c.rect().h / 2, holdMs);
    }
};

void addEffect (GalleryRig<FxUi>& r, int type)
{
    r.tap (r.ui.screen().chainBar.plusCard);
    r.ui.list().state.current = type - 1;
    r.ui.list().showCurrent();
    const ui::Rect row = r.ui.list().rowRect (type - 1);
    r.tapAt (row.x + row.w / 2, row.y + row.h / 2);
}

void renderState (const std::string& outDir, const std::string& state, GalleryInst& baseInst)
{
    std::printf ("Rendering state: %s ...\n", state.c_str());

    // 1. Play 1-3
    savePlay (outDir + "/" + state + "_play1.png", 1, baseInst);
    savePlay (outDir + "/" + state + "_play2.png", 2, baseInst);
    savePlay (outDir + "/" + state + "_play3.png", 3, baseInst);

    // 2. Macros (both banks)
    {
        GalleryRig<MacroUi> r1 (baseInst, 1);
        r1.ui.setBank (0);
        r1.refresh (2);
        r1.picture (outDir + "/" + state + "_macros_bank1.png");
        r1.picture (outDir + "/" + state + "_macros.png");

        GalleryRig<MacroUi> r2 (baseInst, 1);
        r2.ui.setBank (1);
        r2.refresh (2);
        r2.picture (outDir + "/" + state + "_macros_bank2.png");
    }

    // 3. Oscillators
    {
        // Osc A
        GalleryRig<OscPageUi> rOscA (baseInst, 2);
        rOscA.refresh (2);
        rOscA.picture (outDir + "/" + state + "_osca.png");

        // Osc A unison
        rOscA.stack.openDetail (OscUi::kPageUnison);
        rOscA.refresh (2);
        rOscA.picture (outDir + "/" + state + "_osca_unison.png");

        // Osc A modulation
        GalleryRig<OscPageUi> rOscAMod (baseInst, 2);
        rOscAMod.refresh (2);
        const ui::Rect rc = rOscAMod.ui.ui().full().basic.level.rect();
        rOscAMod.tapAt (rc.x + rc.w / 2, rc.y + rc.h / 2, 700);
        rOscAMod.refresh (2);
        if (rOscAMod.stack.top() != OscUi::kPageMods)
            rOscAMod.stack.openDetail (OscUi::kPageMods);
        rOscAMod.refresh (2);
        rOscAMod.picture (outDir + "/" + state + "_osca_mod.png");

        // Osc B, C, Sub, Noise
        GalleryRig<OscPageUi> rOscB (baseInst, 3); rOscB.refresh (2); rOscB.picture (outDir + "/" + state + "_oscb.png");
        GalleryRig<OscPageUi> rOscC (baseInst, 4); rOscC.refresh (2); rOscC.picture (outDir + "/" + state + "_oscc.png");
        GalleryRig<OscPageUi> rOscSub (baseInst, 1); rOscSub.refresh (2); rOscSub.picture (outDir + "/" + state + "_oscsub.png");
        GalleryRig<OscPageUi> rOscNoise (baseInst, 5); rOscNoise.refresh (2); rOscNoise.picture (outDir + "/" + state + "_oscnoise.png");
    }

    // 4. Mixer
    {
        GalleryRig<MixerUi> rMixer (baseInst, 1);
        rMixer.refresh (2);
        rMixer.picture (outDir + "/" + state + "_mixer.png");
    }

    // 5. Filter 1 / 2
    {
        GalleryRig<FilterUi> rFlt1 (baseInst, 1);
        int idx[2];
        synthyboy::FilterResponse::readoutIndices (1, idx);
        float live1[4] = { rFlt1.inst.param (idx[0]), rFlt1.inst.param (idx[1]), 0.0f, 0.0f };
        rFlt1.in.targets = live1;
        rFlt1.refresh (2);
        rFlt1.picture (outDir + "/" + state + "_filter1.png");

        GalleryRig<FilterUi> rFlt2 (baseInst, 2);
        synthyboy::FilterResponse::readoutIndices (2, idx);
        float live2[4] = { 0.0f, 0.0f, rFlt2.inst.param (idx[0]), rFlt2.inst.param (idx[1]) };
        rFlt2.in.targets = live2;
        rFlt2.refresh (2);
        rFlt2.picture (outDir + "/" + state + "_filter2.png");
    }

    // 6. Env 1 (with and without targets) and Env 2..4
    {
        GalleryInst noTargetInst = baseInst;
        for (int i = 1; i <= 64; ++i)
        {
            char k[24]; std::snprintf (k, sizeof k, "mod%02d.source", i);
            if (static_cast<int> (noTargetInst.get (k)) == 1)
                noTargetInst.set (k, 0.0f);
        }
        GalleryRig<EnvUi> rEnv1No (noTargetInst, 1);
        rEnv1No.refresh (2);
        rEnv1No.picture (outDir + "/" + state + "_env1_notargets.png");

        GalleryInst withTargetInst = baseInst;
        withTargetInst.mod (1, 1, "filter1.cutoff", 45.0f);
        GalleryRig<EnvUi> rEnv1With (withTargetInst, 1);
        rEnv1With.refresh (2);
        rEnv1With.picture (outDir + "/" + state + "_env1_targets.png");
        rEnv1With.picture (outDir + "/" + state + "_env1.png");

        GalleryRig<EnvUi> rEnv2 (baseInst, 2); rEnv2.refresh (2); rEnv2.picture (outDir + "/" + state + "_env2.png");
        GalleryRig<EnvUi> rEnv3 (baseInst, 3); rEnv3.refresh (2); rEnv3.picture (outDir + "/" + state + "_env3.png");
        GalleryRig<EnvUi> rEnv4 (baseInst, 4); rEnv4.refresh (2); rEnv4.picture (outDir + "/" + state + "_env4.png");
    }

    // 7. LFO 1 (Hz, BPM, KEY), random LFO, and LFO 2..4
    {
        GalleryInst lfoHzInst = baseInst;
        lfoHzInst.set ("lfo1.sync", 0.0f);
        GalleryRig<LfoUi> rLfoHz (lfoHzInst, 1);
        rLfoHz.refresh (2);
        rLfoHz.picture (outDir + "/" + state + "_lfo1_hz.png");
        rLfoHz.picture (outDir + "/" + state + "_lfo1.png");

        GalleryInst lfoBpmInst = baseInst;
        lfoBpmInst.set ("lfo1.sync", 1.0f);
        GalleryRig<LfoUi> rLfoBpm (lfoBpmInst, 1);
        rLfoBpm.refresh (2);
        rLfoBpm.picture (outDir + "/" + state + "_lfo1_bpm.png");

        GalleryInst lfoKeyInst = baseInst;
        lfoKeyInst.set ("lfo1.sync", 4.0f);
        GalleryRig<LfoUi> rLfoKey (lfoKeyInst, 1);
        rLfoKey.refresh (2);
        rLfoKey.picture (outDir + "/" + state + "_lfo1_key.png");

        GalleryRig<LfoUi> rLfoRnd (baseInst, 1);
        rLfoRnd.refresh (1);
        const ui::Rect rcell = rLfoRnd.ui.full().bar.rndCells.cell (0);
        rLfoRnd.tapAt (rcell.x + 4, rcell.y + 20);
        rLfoRnd.refresh (2);
        rLfoRnd.picture (outDir + "/" + state + "_lfo_random.png");

        GalleryRig<LfoUi> rLfo2 (baseInst, 2); rLfo2.refresh (2); rLfo2.picture (outDir + "/" + state + "_lfo2.png");
        GalleryRig<LfoUi> rLfo3 (baseInst, 3); rLfo3.refresh (2); rLfo3.picture (outDir + "/" + state + "_lfo3.png");
        GalleryRig<LfoUi> rLfo4 (baseInst, 4); rLfo4.refresh (2); rLfo4.picture (outDir + "/" + state + "_lfo4.png");
    }

    // 8. Mod (empty board and with rows)
    {
        GalleryInst modEmptyInst = baseInst;
        for (int i = 1; i <= 64; ++i)
        {
            char k[24]; std::snprintf (k, sizeof k, "mod%02d.source", i);
            modEmptyInst.set (k, 0.0f);
        }
        GalleryRig<ModUi> rModEmpty (modEmptyInst, 1);
        rModEmpty.refresh (2);
        rModEmpty.picture (outDir + "/" + state + "_mod_empty.png");

        GalleryInst modRowsInst = baseInst;
        bool hasRows = false;
        for (int i = 1; i <= 64; ++i)
        {
            char k[24]; std::snprintf (k, sizeof k, "mod%02d.source", i);
            if (modRowsInst.get (k) > 0.0f) { hasRows = true; break; }
        }
        if (! hasRows)
        {
            modRowsInst.mod (1, 7, "filter1.cutoff", 65.0f, true);
            modRowsInst.mod (2, 1, "osc1.tune", -30.0f, true);
            modRowsInst.mod (3, 19, "osc1.frame", 85.0f);
            modRowsInst.mod (4, 27, "lfo1.rate", 50.0f);
            modRowsInst.mod (5, 23, "master.volume", 40.0f);
        }
        GalleryRig<ModUi> rModRows (modRowsInst, 1);
        rModRows.refresh (2);
        rModRows.picture (outDir + "/" + state + "_mod_rows.png");
        rModRows.picture (outDir + "/" + state + "_mod.png");
    }

    // 9. FX (gallery and each of the 13 effect editors, every page of each)
    {
        // FX Gallery
        GalleryInst emptyFxInst = baseInst;
        for (int i = 1; i <= 16; ++i)
        {
            char k[24]; std::snprintf (k, sizeof k, "fxi%02d.type", i); emptyFxInst.set (k, 0.0f);
        }
        for (const char* pfx : { "chorus", "compressor", "delay", "distortion", "eq", "fxfilter", "flanger", "phaser", "reverb", "hyper_dimension", "utility", "bode", "convolve" })
            emptyFxInst.set (std::string (pfx) + ".on", 0.0f);

        GalleryRig<FxUi> rGal (emptyFxInst, 1);
        rGal.refresh (2);
        rGal.picture (outDir + "/" + state + "_fx_gallery.png");
        rGal.picture (outDir + "/" + state + "_fx.png");

        // 13 effect editors
        static const char* const kFxNames[14] = {
            "", "chorus", "compressor", "delay", "distortion", "eq", "filter",
            "flanger", "phaser", "reverb", "hyperdimension", "utility", "bode", "convolve"
        };
        for (int t = 1; t <= 13; ++t)
        {
            GalleryRig<FxUi> rFx (baseInst, 1);
            rFx.refresh();
            addEffect (rFx, t);
            const int numPages = rFx.ui.screen().slotEditor.numPages();
            for (int p = 0; p < numPages; ++p)
            {
                rFx.ui.screen().slotEditor.setPage (p);
                rFx.refresh (2);
                std::string fname = state + "_fx_" + kFxNames[t];
                if (numPages > 1)
                {
                    rFx.picture (outDir + "/" + fname + "_page" + std::to_string (p + 1) + ".png");
                    if (p == 0)
                        rFx.picture (outDir + "/" + fname + ".png");
                }
                else
                {
                    rFx.picture (outDir + "/" + fname + ".png");
                }
            }
        }
    }

    // 10. Master (both sub-pages)
    {
        GalleryRig<MasterUi> rMasterMain (baseInst, 1);
        rMasterMain.ui.screen().setSide (ui::MasterScreen::SideMain);
        rMasterMain.refresh (2);
        rMasterMain.picture (outDir + "/" + state + "_master_main.png");
        rMasterMain.picture (outDir + "/" + state + "_master.png");

        GalleryRig<MasterUi> rMasterTuning (baseInst, 1);
        rMasterTuning.ui.screen().setSide (ui::MasterScreen::SideTuning);
        rMasterTuning.refresh (2);
        rMasterTuning.picture (outDir + "/" + state + "_master_tuning_mpe.png");
    }

    // 11. Voice
    {
        GalleryRig<VoiceUi> rVoice (baseInst, 1);
        rVoice.refresh (2);
        rVoice.picture (outDir + "/" + state + "_voice.png");
    }

    // Fallback: every view of overlay_layout.h (generated from overlay_layout.json)
    for (int i = 0; i < layout::kNumViews; ++i)
    {
        const layout::View& v = layout::kViews[i];
        std::string viewFile = outDir + "/" + state + "_" + v.name + ".png";
        if (! fs::exists (viewFile))
        {
            if (v.kind == layout::kKindPlay)
            {
                savePlay (viewFile, v.index, baseInst);
            }
            else
            {
                auto pv = pages::make (v);
                if (pv)
                {
                    UiStack st;
                    st.openFull (pv->pageElement());
                    ViewInput in;
                    in.instance = &baseInst;
                    in.io = &baseInst;
                    in.ui = &st;
                    in.live = baseInst.haveLive ? &baseInst.liveValues : nullptr;
                    in.nowMs = 1000.0;
                    std::vector<int> tg;
                    pv->targets (tg);
                    std::vector<float> tgVals;
                    for (int t : tg)
                        tgVals.push_back (baseInst.param (t));
                    in.targets = tgVals.empty() ? nullptr : tgVals.data();
                    pv->update (in);
                    uit::Image im;
                    im.w = 1280; im.h = 800;
                    im.px.assign (1280 * 800, 0u);
                    pv->render (im.px.data(), 1280, 800);
                    compositeAndWritePng (viewFile, im);
                }
            }
        }
    }
}

} // namespace

int main (int argc, char** argv)
{
    std::string outDir;
    std::vector<std::string> presets;
    bool testMode = false;
    bool listMode = false;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--out" && i + 1 < argc)
        {
            outDir = argv[++i];
        }
        else if (arg == "--preset" && i + 1 < argc)
        {
            presets.push_back (argv[++i]);
        }
        else if (arg == "--presets")
        {
            while (i + 1 < argc && argv[i + 1][0] != '-')
                presets.push_back (argv[++i]);
        }
        else if (arg == "--test")
        {
            testMode = true;
        }
        else if (arg == "--list")
        {
            listMode = true;
        }
        else if (arg == "--help" || arg == "-h")
        {
            std::printf ("Usage: %s [--out DIR] [--preset FILE ...] [--test] [--list]\n", argv[0]);
            return 0;
        }
    }

    if (listMode)
    {
        std::printf ("Views from overlay layout (%d):\n", layout::kNumViews);
        for (int i = 0; i < layout::kNumViews; ++i)
            std::printf ("  %s (kind %d, index %d)\n", layout::kViews[i].name, layout::kViews[i].kind, layout::kViews[i].index);
        return 0;
    }

    if (testMode)
    {
        std::printf ("Running ui_gallery_host test...\n");
        std::string tmpDir = "/tmp/synthyboy_ui_gallery_test";
        fs::create_directories (tmpDir);
        GalleryInst inst;
        renderState (tmpDir, "test_init", inst);

        // Verify all 24 views exist and are not empty
        bool ok = true;
        for (int i = 0; i < layout::kNumViews; ++i)
        {
            std::string path = tmpDir + "/test_init_" + layout::kViews[i].name + ".png";
            if (! fs::exists (path) || fs::file_size (path) < 1000)
            {
                std::printf ("FAIL: Missing or empty test image for view: %s\n", layout::kViews[i].name);
                ok = false;
            }
        }
        fs::remove_all (tmpDir);
        if (ok)
        {
            std::printf ("PASS  ui_gallery_host self-test: all %d views rendered successfully\n", layout::kNumViews);
            std::printf ("ui_gallery_host: OK\n");
            return 0;
        }
        return 1;
    }

    if (outDir.empty())
        outDir = "build/ui-gallery";

    fs::create_directories (outDir);

    // Render Init state
    {
        GalleryInst initInst;
        renderState (outDir, "Init", initInst);
    }

    // Default presets if none specified
    if (presets.empty())
    {
        std::vector<std::string> defaults = {
            "content/presets/Bass/scream_filter_bass.sbpreset",
            "content/presets/Pad/tri_band_split_texture.sbpreset",
            "content/presets/Pluck/aux_keyscale_pluck.sbpreset"
        };
        for (const auto& p : defaults)
            if (fs::exists (p))
                presets.push_back (p);
    }

    // Render presets
    for (const auto& p : presets)
    {
        if (! fs::exists (p))
        {
            std::fprintf (stderr, "Warning: Preset file not found: %s\n", p.c_str());
            continue;
        }
        std::string stem = fs::path (p).stem().string();
        GalleryInst inst;
        if (inst.loadPresetFile (p))
        {
            renderState (outDir, stem, inst);
        }
        else
        {
            std::fprintf (stderr, "Error loading preset: %s\n", p.c_str());
        }
    }

    std::printf ("ui_gallery_host: finished rendering to %s\n", outDir.c_str());
    return 0;
}
