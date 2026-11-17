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
#include "synthyboy/content_store.h"
#include "synthyboy/osc_cycle.h"
#include "synthyboy/user_content.h"
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
    const synthyboy::IContentStore* contentStore() const noexcept override { return store_.get(); }
    /** A sample (a decaying chirp with a second burst, so the picture has some shape) as the content of oscillator `osc` (1 .. 3). */
    void addSample (int osc)
    {
        const int rate = 44100;
        const size_t n = static_cast<size_t> (rate * 2);
        std::vector<float> x (n);
        for (size_t i = 0; i < n; ++i)
        {
            const double t = static_cast<double> (i) / rate;
            const double env = std::exp (-2.2 * std::fmod (t, 1.0)) * (t < 1.0 ? 1.0 : 0.6);
            x[i] = static_cast<float> (0.8 * env * std::sin (6.283185307179586 * (110.0 + 220.0 * std::fmod (t, 1.0)) * t));
        }
        if (! store_)
            store_ = std::make_shared<synthyboy::MemoryContentStore>();
        store_->add ("gallery", "s" + std::to_string (osc) + ".wav", synthyboy::encodeWav (x.data(), nullptr, n, rate, 16));
        auto c = content_ ? std::make_shared<synthyboy::StateContent> (*content_) : std::make_shared<synthyboy::StateContent>();
        synthyboy::ContentSample cs;
        cs.osc = osc; cs.ref.pack = "gallery"; cs.ref.file = "s" + std::to_string (osc) + ".wav"; cs.ref.name = "chirp";
        cs.sampleRate = rate; cs.frames = static_cast<int> (n); cs.channels = 1;
        c->samples.push_back (cs);
        content_ = c;
    }
    void setContent (std::shared_ptr<const synthyboy::StateContent> c) { content_ = std::move (c); }
    /** A sample the state refers to (a pack, a long file name, no title) whose file is not there: the content reference without the data, and the engine's missing_content bit of the oscillator (`missingContent`). */
    void addMissingSample (int osc, const std::string& file)
    {
        auto c = content_ ? std::make_shared<synthyboy::StateContent> (*content_) : std::make_shared<synthyboy::StateContent>();
        synthyboy::ContentSample cs;
        cs.osc = osc; cs.ref.pack = "gallery"; cs.ref.file = file;
        c->samples.push_back (cs);
        content_ = c;
        missingBits |= 1u << (osc - 1);
    }
    unsigned missingContent() const noexcept override { return missingBits; }
    unsigned missingBits = 0;

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

    bool padPressure (bool& on, float& percent) const noexcept override
    {
        on = padPressureOn;
        percent = padPressurePercent;
        return true;
    }
    void setPadPressure (bool on, float percent) noexcept override
    {
        padPressureOn = on;
        padPressurePercent = percent;
    }
    bool padPressureOn = false;
    float padPressurePercent = 35.0f;

private:
    float v_[synthyboy::kMaxParams] = {};
    std::shared_ptr<const synthyboy::StateContent> content_;
    std::shared_ptr<synthyboy::MemoryContentStore> store_;
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

inline std::string slugify (const std::string& s)
{
    std::string out;
    for (char c : s)
    {
        if (c >= 'A' && c <= 'Z')
            out.push_back (static_cast<char> (std::tolower (c)));
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            out.push_back (c);
        else if (c == ' ' || c == '-' || c == '_')
        {
            if (! out.empty() && out.back() != '_')
                out.push_back ('_');
        }
    }
    while (! out.empty() && out.back() == '_')
        out.pop_back();
    return out;
}

void savePlay (const std::string& path, int highlight, GalleryInst& inst,
               int oscSel = 2, int envSel = 1, int lfoSlot = 1,
               int w = 1280, int h = 800, const std::function<void (PlayView&, UiStack&)>& act = nullptr)
{
    const int savedOsc = inst.playSelection (0);
    const int savedEnv = inst.playSelection (1);
    const int savedLfo = inst.playSelection (2);

    inst.setPlaySelection (0, oscSel);
    inst.setPlaySelection (1, envSel);
    inst.setPlaySelection (2, lfoSlot >= 10 ? 1 : lfoSlot);

    PlayView pv (highlight, 0.0);
    UiStack stack;
    ViewInput in;
    in.instance = &inst;
    in.io = &inst;
    in.ui = &stack;
    in.live = inst.haveLive ? &inst.liveValues : nullptr;
    in.nowMs = 1000.0;
    pv.update (in);
    if (lfoSlot >= 10 && pv.lfoUi() != nullptr)
    {
        pv.lfoUi()->select (lfoSlot, inst);
    }
    in.nowMs = 1500.0;
    pv.update (in);
    if (act)   // a touch on the page (the type list opened, ...), then the picture
    {
        act (pv, stack);
        in.nowMs = 1600.0;
        pv.update (in);
    }

    uit::Image im;
    im.w = w; im.h = h;
    im.px.assign (static_cast<size_t> (w) * static_cast<size_t> (h), 0u);
    pv.render (im.px.data(), w, h);
    compositeAndWritePng (path, im);

    inst.setPlaySelection (0, savedOsc);
    inst.setPlaySelection (1, savedEnv);
    inst.setPlaySelection (2, savedLfo);
}

template <class Ui>
struct GalleryRig
{
    Ui ui;
    GalleryInst inst;
    UiStack stack;
    page::Input in;
    double now = 1000.0;
    MacroNames names;   // the macro names of the instance, active for everything the rig does (as the page view does)

    explicit GalleryRig (const GalleryInst& baseInst, int instance = 1)
        : inst (baseInst)
    {
        in.instance = instance;
        stack.openFull (Ui::kElementPage);
    }

    void refresh (int times = 2)
    {
        names.load (inst.content().get());
        const NamesScope scope (names);
        for (int i = 0; i < times; ++i)
        {
            now += 150.0;
            in.live = inst.haveLive ? &inst.liveValues : nullptr;
            ui.update (inst, stack, now, in);
        }
    }

    void picture (const std::string& path)
    {
        const NamesScope scope (names);
        saveScene (path, [this] (gpu::SceneBuilder& sb) { ui.draw (sb); });
    }

    void tapAt (int x, int y, int holdMs = 40)
    {
        const NamesScope scope (names);
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

void renderState (const std::string& outDir, const std::string& state, GalleryInst& baseInst, const std::string& onlyPrefix = "")
{
    std::printf ("Rendering state: %s ...\n", state.c_str());

    auto shouldRender = [&] (const std::string& name) -> bool
    {
        if (onlyPrefix.empty())
            return true;
        if (state.rfind (onlyPrefix, 0) == 0)
            return true;
        const std::string full = state + "_" + name;
        if (full.rfind (onlyPrefix, 0) == 0)
            return true;
        if (name.rfind (onlyPrefix, 0) == 0)
            return true;
        return false;
    };

    // 1. Play 1-3
    if (shouldRender ("play1.png")) savePlay (outDir + "/" + state + "_play1.png", 1, baseInst);
    if (shouldRender ("play2.png")) savePlay (outDir + "/" + state + "_play2.png", 2, baseInst);
    if (shouldRender ("play3.png")) savePlay (outDir + "/" + state + "_play3.png", 3, baseInst);

    // (a0) the Play page (2026-11-18): oscillator A, Sub and Noise switched off (grey display and controls, the power button red), the selectors (carousels) at the first, a middle and the last entry of each third,
    // the type drop-down closed (the compact views above) and open for A, Sub and Noise
    {
        struct Off { const char* name; const char* key; int sel; };
        for (const Off& o : { Off { "osca", "osc1.on", playosc::kA }, Off { "sub", "sub.on", playosc::kSub }, Off { "noise", "noise.on", playosc::kNoise } })
            if (shouldRender (std::string ("play1_off_") + o.name + ".png"))
            {
                GalleryInst off = baseInst;
                off.set (o.key, 0.0f);
                savePlay (outDir + "/" + state + "_play1_off_" + o.name + ".png", 1, off, o.sel, 1, 1);
            }
        const int oscSel[3] = { playosc::kSub, playosc::kB, playosc::kNoise }, envSel[3] = { 1, 3, 6 }, lfoSel[3] = { 1, 5, 14 };
        const char* const pos[3] = { "first", "middle", "last" };
        for (int i = 0; i < 3; ++i)
            if (shouldRender (std::string ("play1_carousel_") + pos[i] + ".png"))
                savePlay (outDir + "/" + state + "_play1_carousel_" + pos[i] + ".png", 1, baseInst, oscSel[i], envSel[i], lfoSel[i]);
        struct Ty { const char* name; int sel; };
        for (const Ty& t : { Ty { "osca", playosc::kA }, Ty { "sub", playosc::kSub }, Ty { "noise", playosc::kNoise } })
            if (shouldRender (std::string ("play1_type_open_") + t.name + ".png"))
                savePlay (outDir + "/" + state + "_play1_type_open_" + t.name + ".png", 1, baseInst, t.sel, 1, 1, 1280, 800,
                          [&baseInst] (PlayView& pv, UiStack& st)
                          {
                              OscUi* o = pv.oscUi();
                              if (o == nullptr) return;
                              GalleryInst io = baseInst;
                              const ui::Rect r = o->compact().basic.selector.rect();
                              TouchEvent d; d.kind = TouchEvent::Down; d.x = r.x + r.w / 2; d.y = r.y + r.h / 2;
                              TouchEvent u = d; u.kind = TouchEvent::Up;
                              pv.onTouch (d, st, io, 1550.0);
                              pv.onTouch (u, st, io, 1590.0);
                          });
    }

    // (a) Play 1-3 for each of the five oscillator sources, envelope 1 and 6, and LFO 1, 10, R1
    for (int p = 1; p <= 3; ++p)
    {
        const std::string pstr = std::to_string (p);
        // Oscillator sources: Sub, Osc A, Osc B, Osc C, Noise
        if (shouldRender ("play" + pstr + "_src_sub.png"))
            savePlay (outDir + "/" + state + "_play" + pstr + "_src_sub.png", p, baseInst, playosc::kSub, 1, 1);
        if (shouldRender ("play" + pstr + "_src_osca.png"))
            savePlay (outDir + "/" + state + "_play" + pstr + "_src_osca.png", p, baseInst, playosc::kA, 1, 1);
        if (shouldRender ("play" + pstr + "_src_oscb.png"))
            savePlay (outDir + "/" + state + "_play" + pstr + "_src_oscb.png", p, baseInst, playosc::kB, 1, 1);
        if (shouldRender ("play" + pstr + "_src_oscc.png"))
            savePlay (outDir + "/" + state + "_play" + pstr + "_src_oscc.png", p, baseInst, playosc::kC, 1, 1);
        if (shouldRender ("play" + pstr + "_src_noise.png"))
            savePlay (outDir + "/" + state + "_play" + pstr + "_src_noise.png", p, baseInst, playosc::kNoise, 1, 1);

        // Envelopes 1 and 6
        if (shouldRender ("play" + pstr + "_env1.png"))
            savePlay (outDir + "/" + state + "_play" + pstr + "_env1.png", p, baseInst, playosc::kA, 1, 1);
        if (shouldRender ("play" + pstr + "_env6.png"))
            savePlay (outDir + "/" + state + "_play" + pstr + "_env6.png", p, baseInst, playosc::kA, 6, 1);

        // LFO 1, 10, R1 (slot 10 in LfoUi is R1)
        if (shouldRender ("play" + pstr + "_lfo1.png"))
            savePlay (outDir + "/" + state + "_play" + pstr + "_lfo1.png", p, baseInst, playosc::kA, 1, 1);
        if (shouldRender ("play" + pstr + "_lfo10.png"))
            savePlay (outDir + "/" + state + "_play" + pstr + "_lfo10.png", p, baseInst, playosc::kA, 1, 10);
        if (shouldRender ("play" + pstr + "_lfoR1.png"))
            savePlay (outDir + "/" + state + "_play" + pstr + "_lfoR1.png", p, baseInst, playosc::kA, 1, 10);
    }

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
        // Osc A in each type, empty and with a file loaded, and the detail pages of the types (docs/MPC.md "Oscillator UI", table type -> page -> block -> controls)
        {
            struct T { const char* name; int type; };
            const T types[] = { { "wave", 0 }, { "multi", 4 }, { "sample", 1 }, { "grain", 2 }, { "spec", 3 } };
            for (const T& t : types)
                for (int loaded = 0; loaded < (t.type == 0 ? 1 : 2); ++loaded)
                {
                    GalleryInst gi = baseInst;
                    gi.set ("osc1.type", static_cast<float> (t.type));
                    if (loaded && t.type != 4)
                        gi.addSample (1);
                    GalleryRig<OscPageUi> r (gi, 2);
                    r.refresh (2);
                    r.picture (outDir + "/" + state + "_osca_type_" + t.name + (loaded ? "_loaded" : "") + ".png");
                    struct P { int page; const char* name; };
                    std::vector<P> pages = { { 17, "more" } };
                    if (t.type == 0) pages.push_back ({ 16, "map" });
                    if (t.type == 1) { pages.push_back ({ 18, "region" }); pages.push_back ({ 19, "slice" }); }
                    if (t.type == 2) pages.push_back ({ 20, "grain" });
                    if (t.type == 3) { pages.push_back ({ 21, "filter" }); pages.push_back ({ 22, "scan" }); }
                    for (const P& pg : pages)
                    {
                        if (pg.page == 17 && loaded) continue;
                        GalleryInst g2 = gi;
                        if (pg.page == 18) { g2.set ("osc1.sample.start", 12.0f); g2.set ("osc1.sample.end", 88.0f); g2.set ("osc1.sample.loop_mode", 1.0f); g2.set ("osc1.sample.loop_start", 30.0f); g2.set ("osc1.sample.loop_end", 70.0f); }
                        if (pg.page == 19) { g2.set ("osc1.sample.slice_mode", 1.0f); g2.set ("osc1.sample.scan_lock_tempo", 1.0f); }
                        if (pg.page == 20) { g2.set ("osc1.gran.density_mode", 1.0f); g2.set ("osc1.gran.length_mode", 2.0f); }
                        if (pg.page == 21) { g2.set ("osc1.spec.flt_use", 1.0f); g2.set ("osc1.spec.freq_lo", 200.0f); g2.set ("osc1.spec.freq_hi", 5000.0f); }
                        if (pg.page == 16) { g2.set ("osc1.remap.on", 1.0f); }
                        GalleryRig<OscPageUi> rp (g2, 2);
                        rp.refresh (2);
                        rp.stack.openDetail (pg.page);
                        rp.refresh (2);
                        rp.picture (outDir + "/" + state + "_osca_" + t.name + "_" + pg.name + (loaded ? "_loaded" : "") + ".png");
                    }
                }
        }
        // Osc A: a sample type whose file is missing: "content missing: <name>" on the display (the name from the state's reference, underscores as spaces, the middle goes when it is too long)
        {
            GalleryInst gm = baseInst;
            gm.set ("osc1.type", 1.0f);
            gm.addMissingSample (1, "packs/drums/drum_loop_120bpm_breakbeat_long_file_name_final_take.wav");
            GalleryRig<OscPageUi> rm (gm, 2);
            rm.refresh (2);
            rm.picture (outDir + "/" + state + "_osca_missing_content.png");
        }
        GalleryRig<OscPageUi> rOscSub (baseInst, 1); rOscSub.refresh (2); rOscSub.picture (outDir + "/" + state + "_oscsub.png");
        GalleryRig<OscPageUi> rOscNoise (baseInst, 5); rOscNoise.refresh (2); rOscNoise.picture (outDir + "/" + state + "_oscnoise.png");

        // (b) Full Sub and Noise pages with each LED toggle on and off
        {
            struct ToggleDef { const char* key; const char* name; };
            const ToggleDef subToggles[] = {
                { "sub.env1", "env1" },
                { "sub.mono", "mono" },
                { "sub.phase_contiguous", "freerun" },
            };
            for (const auto& tg : subToggles)
            {
                if (shouldRender (std::string ("oscsub_") + tg.name + "_on.png"))
                {
                    GalleryInst gi = baseInst;
                    gi.set (tg.key, 1.0f);
                    GalleryRig<OscPageUi> r (gi, 1);
                    r.refresh (2);
                    r.picture (outDir + "/" + state + "_oscsub_" + tg.name + "_on.png");
                }
                if (shouldRender (std::string ("oscsub_") + tg.name + "_off.png"))
                {
                    GalleryInst gi = baseInst;
                    gi.set (tg.key, 0.0f);
                    GalleryRig<OscPageUi> r (gi, 1);
                    r.refresh (2);
                    r.picture (outDir + "/" + state + "_oscsub_" + tg.name + "_off.png");
                }
            }
            if (shouldRender ("oscsub_all_on.png"))
            {
                GalleryInst gi = baseInst;
                gi.set ("sub.env1", 1.0f); gi.set ("sub.mono", 1.0f); gi.set ("sub.phase_contiguous", 1.0f);
                GalleryRig<OscPageUi> r (gi, 1);
                r.refresh (2);
                r.picture (outDir + "/" + state + "_oscsub_all_on.png");
            }
            if (shouldRender ("oscsub_all_off.png"))
            {
                GalleryInst gi = baseInst;
                gi.set ("sub.env1", 0.0f); gi.set ("sub.mono", 0.0f); gi.set ("sub.phase_contiguous", 0.0f);
                GalleryRig<OscPageUi> r (gi, 1);
                r.refresh (2);
                r.picture (outDir + "/" + state + "_oscsub_all_off.png");
            }

            const ToggleDef noiseGenToggles[] = {
                { "noise.env1", "env1" },
                { "noise.keytrack", "keytrack" },
            };
            for (const auto& tg : noiseGenToggles)
            {
                if (shouldRender (std::string ("oscnoise_") + tg.name + "_on.png"))
                {
                    GalleryInst gi = baseInst;
                    gi.set ("noise.type", 0.0f);
                    gi.set (tg.key, 1.0f);
                    GalleryRig<OscPageUi> r (gi, 5);
                    r.refresh (2);
                    r.picture (outDir + "/" + state + "_oscnoise_" + tg.name + "_on.png");
                }
                if (shouldRender (std::string ("oscnoise_") + tg.name + "_off.png"))
                {
                    GalleryInst gi = baseInst;
                    gi.set ("noise.type", 0.0f);
                    gi.set (tg.key, 0.0f);
                    GalleryRig<OscPageUi> r (gi, 5);
                    r.refresh (2);
                    r.picture (outDir + "/" + state + "_oscnoise_" + tg.name + "_off.png");
                }
            }

            const ToggleDef noiseSampleToggles[] = {
                { "noise.env1", "env1" },
                { "noise.keytrack", "keytrack" },
                { "noise.random_phase", "rand" },
                { "noise.loop", "loop" },
                { "noise.bounce", "bounce" },
            };
            for (const auto& tg : noiseSampleToggles)
            {
                if (shouldRender (std::string ("oscnoise_sample_") + tg.name + "_on.png"))
                {
                    GalleryInst gi = baseInst;
                    gi.set ("noise.type", 1.0f);
                    gi.set (tg.key, 1.0f);
                    GalleryRig<OscPageUi> r (gi, 5);
                    r.refresh (2);
                    r.picture (outDir + "/" + state + "_oscnoise_sample_" + tg.name + "_on.png");
                }
                if (shouldRender (std::string ("oscnoise_sample_") + tg.name + "_off.png"))
                {
                    GalleryInst gi = baseInst;
                    gi.set ("noise.type", 1.0f);
                    gi.set (tg.key, 0.0f);
                    GalleryRig<OscPageUi> r (gi, 5);
                    r.refresh (2);
                    r.picture (outDir + "/" + state + "_oscnoise_sample_" + tg.name + "_off.png");
                }
            }
        }
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
        {   // the OUTPUT popup of the filter (destination, buses, level, pan)
            GalleryRig<FilterUi> rOut (baseInst, 1);
            rOut.in.targets = live1;
            rOut.refresh (2);
            rOut.stack.openDetail (FilterUi::kPageRoute);
            rOut.refresh (2);
            rOut.picture (outDir + "/" + state + "_filter1_output.png");
        }

        // (c) Filter 1 with every filter model once (model name in file name)
        const auto* pModel = synthyboy::findParamByKey ("filter1.model");
        const int numModels = pModel ? pModel->numChoices : 27;
        for (int m = 0; m < numModels; ++m)
        {
            const std::string mname = (pModel && m < pModel->numChoices) ? slugify (pModel->choices[m]) : std::to_string (m);
            const std::string ftarget = "filter1_model_" + mname + ".png";
            if (shouldRender (ftarget))
            {
                GalleryInst gi = baseInst;
                gi.set ("filter1.model", static_cast<float> (m));
                if (m == 5) // Formant
                {
                    gi.set ("filter1.formant.x", 45.0f);
                    gi.set ("filter1.formant.y", 75.0f);
                    gi.set ("filter1.formant.resonance", 60.0f);
                    gi.set ("filter1.formant.spread", 30.0f);
                }
                else if (m == 6) // Comb
                {
                    gi.set ("filter1.comb_offset", 35.0f);
                }
                GalleryRig<FilterUi> rFlt (gi, 1);
                float live[4] = { rFlt.inst.param (idx[0]), rFlt.inst.param (idx[1]), 0.0f, 0.0f };
                rFlt.in.targets = live;
                rFlt.refresh (2);
                rFlt.picture (outDir + "/" + state + "_" + ftarget);
            }
        }

        // (c) Style list open for one classic (Ladder, model 2) and one Serum model (Scream, model 16)
        if (shouldRender ("filter1_style_list_classic.png") || shouldRender ("filter1_style_list.png"))
        {
            GalleryInst gi = baseInst;
            gi.set ("filter1.model", 2.0f); // Ladder (classic)
            GalleryRig<FilterUi> rStyleClassic (gi, 1);
            float live[4] = { rStyleClassic.inst.param (idx[0]), rStyleClassic.inst.param (idx[1]), 0.0f, 0.0f };
            rStyleClassic.in.targets = live;
            rStyleClassic.refresh (2);
            rStyleClassic.tap (rStyleClassic.ui.full().style);
            rStyleClassic.picture (outDir + "/" + state + "_filter1_style_list_classic.png");
            rStyleClassic.picture (outDir + "/" + state + "_filter1_style_list.png");
        }
        if (shouldRender ("filter1_style_list_serum.png"))
        {
            GalleryInst gi = baseInst;
            gi.set ("filter1.model", 16.0f); // Scream (Serum model)
            GalleryRig<FilterUi> rStyleSerum (gi, 1);
            float live[4] = { rStyleSerum.inst.param (idx[0]), rStyleSerum.inst.param (idx[1]), 0.0f, 0.0f };
            rStyleSerum.in.targets = live;
            rStyleSerum.refresh (2);
            rStyleSerum.tap (rStyleSerum.ui.full().style);
            rStyleSerum.picture (outDir + "/" + state + "_filter1_style_list_serum.png");
        }

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

        // the LFO Editor (opened by the EDIT button at the top of the button column, for every shape): empty (a drawn shape without points), a preset shape taken over as points, a drawn shape, each tool
        {
            auto openEditor = [&] (float shape) -> std::unique_ptr<GalleryRig<LfoUi>>
            {
                GalleryInst gi = baseInst;
                gi.set ("lfo1.shape", shape);
                auto r = std::make_unique<GalleryRig<LfoUi>> (gi, 1);
                r->refresh (2);
                r->tap (r->ui.full().edit);
                r->refresh (2);
                return r;
            };
            auto at = [] (GalleryRig<LfoUi>& r, float fx, float fy) { r.tapAt (static_cast<int> (r.ui.editor().canvas.xOf (fx)), static_cast<int> (r.ui.editor().canvas.yOf (fy))); };
            auto tool = [] (GalleryRig<LfoUi>& r, int t) { const ui::Rect c = r.ui.editor().tools.cell (t); r.tapAt (c.x + c.w / 2, c.y + c.h / 2); };
            openEditor (12.0f)->picture (outDir + "/" + state + "_lfo_editor_empty.png");
            openEditor (0.0f)->picture (outDir + "/" + state + "_lfo_editor_sine.png");
            openEditor (4.0f)->picture (outDir + "/" + state + "_lfo_editor_square.png");
            auto drawn = [&] (const char* name, int toolIndex)
            {
                auto r = openEditor (12.0f);
                r->ui.editor().canvas.state.gridX = 8;   // a coarse grid the picture shows well: 8 columns, 4 rows
                r->ui.editor().canvas.state.gridY = 4;
                at (*r, 0.25f, 0.9f); at (*r, 0.5f, 0.2f); at (*r, 0.75f, 0.7f);
                at (*r, 0.5f, 0.2f);   // select the middle point: the fields show it
                if (toolIndex == 1) { tool (*r, 1); at (*r, 0.6f, 0.5f); }
                if (toolIndex == 2) { tool (*r, 2); at (*r, 0.6f, 0.3f); }
                if (toolIndex == 3) { tool (*r, 3); at (*r, 0.6f, 0.8f); }
                if (toolIndex == 0) tool (*r, 0);
                r->refresh (2);
                r->picture (outDir + "/" + state + "_lfo_editor_" + name + ".png");
            };
            drawn ("tool_point", 0);
            drawn ("tool_flat", 1);
            drawn ("tool_rampup", 2);
            drawn ("tool_rampdown", 3);
        }

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
        {   // the page of one modulation slot (the first row of the list)
            GalleryRig<ModUi> rSlot (modRowsInst, 1);
            rSlot.refresh (2);
            rSlot.ui.openSlotDetail (0, rSlot.stack);
            rSlot.refresh (2);
            rSlot.picture (outDir + "/" + state + "_mod_slot.png");
        }
    }

    // 9. FX (gallery and each of the 16 effect editors, every page of each)
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

        // 16 effect editors (the 13 types and the 3 splitters)
        static const char* const kFxNames[17] = {
            "", "chorus", "compressor", "delay", "distortion", "eq", "filter",
            "flanger", "phaser", "reverb", "hyperdimension", "utility", "bode", "convolve",
            "splitter_lh", "splitter_lmh", "splitter_ms"   // the three splitters: modules of the fixed chain, added from the same list
        };
        for (int t = 1; t <= 16; ++t)
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

    // 9b. The racks of the bands of the three splitters: the splitter page with its lanes (effects parked in the bands), the page of a band with its first effect, the empty band
    {
        static const char* const kSplitNames[3] = { "splitter_lh", "splitter_lmh", "splitter_ms" };
        for (int si = 0; si < 3; ++si)
        {
            const int kind = ui::fxl::kKindSplitterLh + si;
            GalleryInst inst = baseInst;
            for (int t = 1; t < synthyboy::kNumFxInstanceTypes; ++t)
            {
                inst.set (std::string (synthyboy::fxTypeDescriptor (t).prefix) + ".on", 0.0f);
                inst.set (std::string (synthyboy::fxTypeDescriptor (t).prefix) + ".rack", 0.0f);
            }
            for (int i = 1; i <= 16; ++i)
            {
                char k[24]; std::snprintf (k, sizeof k, "fxi%02d.type", i); inst.set (k, 0.0f);
            }
            static const int kBandTypes[7][3] = { { synthyboy::kFxTypeCompressor, synthyboy::kFxTypeDistortion, 0 }, { synthyboy::kFxTypeEq, synthyboy::kFxTypeChorus, synthyboy::kFxTypeReverb },
                                                  { synthyboy::kFxTypeUtility, synthyboy::kFxTypeCompressor, 0 }, { synthyboy::kFxTypeFilter, synthyboy::kFxTypePhaser, synthyboy::kFxTypeDelay },
                                                  { synthyboy::kFxTypeEq, synthyboy::kFxTypeFlanger, 0 }, { synthyboy::kFxTypeUtility, 0, 0 }, { synthyboy::kFxTypeBode, synthyboy::kFxTypeHyperDimension, 0 } };
            bool used[synthyboy::kNumFxInstanceTypes] = {};
            for (int b = 0; b < ui::fxl::bandsOfSplitter (kind); ++b)
            {
                const int rack = ui::fxl::bandRack (kind, b);
                for (int t : kBandTypes[rack - ui::fxl::kFirstBandRack])
                    if (t > 0 && ! used[t])
                    {
                        used[t] = true;
                        inst.set (std::string (synthyboy::fxTypeDescriptor (t).prefix) + ".on", b == 1 && t == synthyboy::kFxTypeChorus ? 0.0f : 1.0f);
                        inst.set (std::string (synthyboy::fxTypeDescriptor (t).prefix) + ".rack", static_cast<float> (rack));
                    }
            }
            inst.set (std::string (kSplitNames[si]) + ".on", 1.0f);
            GalleryRig<FxUi> rs (inst, 1);
            rs.refresh (2);
            rs.picture (outDir + "/" + state + "_fx_" + kSplitNames[si] + "_lanes.png");
            const int nb = ui::fxl::bandsOfSplitter (kind);
            for (int b = 0; b < nb; ++b)
            {
                rs.tap (rs.ui.screen().slotEditor.bands.lane[b].tile);
                rs.picture (outDir + "/" + state + "_fx_" + kSplitNames[si] + "_band" + std::to_string (b + 1) + ".png");
                rs.tap (rs.ui.screen().chainBar.backCard);
            }
            // an empty band: the gallery of the free types
            GalleryInst bare = inst;
            for (int t = 1; t < synthyboy::kNumFxInstanceTypes; ++t)
            {
                bare.set (std::string (synthyboy::fxTypeDescriptor (t).prefix) + ".rack", 0.0f);
                bare.set (std::string (synthyboy::fxTypeDescriptor (t).prefix) + ".on", 0.0f);
            }
            GalleryRig<FxUi> re (bare, 1);
            re.refresh (2);
            re.tap (re.ui.screen().slotEditor.bands.lane[0].tile);
            re.picture (outDir + "/" + state + "_fx_" + kSplitNames[si] + "_band1_empty.png");
        }
    }

    // 9c. (d) Every FX effect page once with the effect in a splitter band too (one picture per splitter kind)
    {
        static const char* const kSplitKinds[3] = { "splitter_lh", "splitter_lmh", "splitter_ms" };
        for (int si = 0; si < 3; ++si)
        {
            const int kind = ui::fxl::kKindSplitterLh + si;
            const int rack = ui::fxl::bandRack (kind, 0);

            static const char* const kFxNames[14] = { "", "chorus", "compressor", "delay", "distortion", "eq", "filter", "flanger", "phaser", "reverb", "hyperdimension", "utility", "bode", "convolve" };
            for (int t = 1; t <= 13; ++t)
            {
                const std::string baseName = "fx_" + std::string (kFxNames[t]) + "_" + kSplitKinds[si];
                if (! shouldRender (baseName + ".png") && ! shouldRender (baseName + "_page1.png"))
                    continue;

                GalleryInst inst = baseInst;
                for (int i = 1; i < synthyboy::kNumFxInstanceTypes; ++i)
                {
                    inst.set (std::string (synthyboy::fxTypeDescriptor (i).prefix) + ".on", 0.0f);
                    inst.set (std::string (synthyboy::fxTypeDescriptor (i).prefix) + ".rack", 0.0f);
                }
                for (int i = 1; i <= 16; ++i)
                {
                    char k[24]; std::snprintf (k, sizeof k, "fxi%02d.type", i); inst.set (k, 0.0f);
                }

                inst.set (std::string (kSplitKinds[si]) + ".on", 1.0f);
                const char* pre = synthyboy::fxTypeDescriptor (t).prefix;
                inst.set (std::string (pre) + ".on", 1.0f);
                inst.set (std::string (pre) + ".rack", static_cast<float> (rack));

                GalleryRig<FxUi> rFx (inst, 1);
                rFx.refresh (2);

                if (! rFx.ui.screen().slotEditor.bands.lane[0].cards[0].rect().empty())
                {
                    rFx.tap (rFx.ui.screen().slotEditor.bands.lane[0].cards[0]);
                    rFx.refresh (2);
                }
                else
                {
                    rFx.tap (rFx.ui.screen().slotEditor.bands.lane[0].tile);
                    addEffect (rFx, t);
                    rFx.refresh (2);
                }

                const int numPages = rFx.ui.screen().slotEditor.numPages();
                for (int p = 0; p < numPages; ++p)
                {
                    rFx.ui.screen().slotEditor.setPage (p);
                    rFx.refresh (2);
                    std::string fname = state + "_" + baseName;
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

        // (e) Master page with PAD PRESS switch on
        if (shouldRender ("master_pad_press.png"))
        {
            GalleryRig<MasterUi> rMasterPad (baseInst, 1);
            rMasterPad.inst.padPressureOn = true;
            rMasterPad.ui.screen().setSide (ui::MasterScreen::SideMain);
            rMasterPad.refresh (2);
            rMasterPad.picture (outDir + "/" + state + "_master_pad_press.png");
            rMasterPad.picture (outDir + "/" + state + "_master_pad_press_on.png");
        }
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
    // 16 knob device (the MPC XL): the idle chips of the other groups show the knob numbers 5 .. 16 (QGroup::setWideDevice, as overlay/service.h does on the XL); pictures named *_wide16_*
    {
        const bool wasWide = ui::QGroup::wideDevice();   // (the device of the run: the XL by default; the rest of the run, the next states included, keeps it)
        ui::QGroup::setWideDevice (true);
        GalleryRig<EnvUi> rW1 (baseInst, 1); rW1.refresh (2); rW1.picture (outDir + "/" + state + "_wide16_env1.png");
        GalleryRig<LfoUi> rW2 (baseInst, 1); rW2.refresh (2); rW2.picture (outDir + "/" + state + "_wide16_lfo1.png");
        GalleryRig<FilterUi> rW3 (baseInst, 1); rW3.refresh (2); rW3.picture (outDir + "/" + state + "_wide16_filter1.png");
        GalleryRig<MasterUi> rW4 (baseInst, 1); rW4.refresh (2); rW4.picture (outDir + "/" + state + "_wide16_master.png");
        GalleryRig<MixerUi> rW5 (baseInst, 1); rW5.refresh (2); rW5.picture (outDir + "/" + state + "_wide16_mixer.png");
        GalleryRig<OscPageUi> rW6 (baseInst, 2); rW6.refresh (2); rW6.picture (outDir + "/" + state + "_wide16_osca.png");
        ui::QGroup::setWideDevice (wasWide);
    }
}

} // namespace

/** The names of the macros (docs/ui-design/macro-names.md): the pages with named macros, and the keyboard page in its states. */
void renderNames (const std::string& outDir)
{
    auto content = std::make_shared<synthyboy::StateContent>();
    const char* const names[8] = { "Filter Sweep", "Drive + Warp", "Cutoff", "Detune + Chorus", "", "Reso", "WWWWWWWWWWWWWWWW", "Reverb + Chorus" };
    for (int i = 0; i < 8; ++i)
        content->macroNames[i] = names[i];
    GalleryInst inst;
    inst.setContent (content);
    renderState (outDir, "Named", inst);   // no slot in use: the Mod page shows the board of the sources

    // macros that drive knobs: the chips of the knobs, the rows of the Mod page, the targets pages
    GalleryInst driven = inst;
    driven.mod (1, uibind::macroSource (1), "filter1.cutoff", 45.0f);
    driven.mod (2, uibind::macroSource (3), "filter1.cutoff", 30.0f);
    driven.mod (3, uibind::macroSource (6), "filter1.resonance", 30.0f);
    driven.mod (4, uibind::macroSource (2), "distortion.drive", 35.0f);
    driven.mod (5, uibind::macroSource (7), "osc1.level", 20.0f);
    driven.mod (6, uibind::macroSource (4), "osc1.unison.detune", 25.0f);
    driven.mod (7, uibind::macroSource (8), "reverb.mix", 50.0f);
    driven.mod (8, uibind::macroSource (5), "osc1.pan", 20.0f);
    renderState (outDir, "NamedMods", driven);
    {
        GalleryRig<MacroUi> r (driven, 1);
        r.refresh();
        r.tapAt (r.ui.view().cards[0].chipsRect().x + 20, r.ui.view().cards[0].chipsRect().y + 20);
        r.refresh();
        r.picture (outDir + "/NamedMods_macro_targets.png");
    }

    // the keyboard page of an unnamed macro: empty, with text, caps lock, the second layer, the limit
    GalleryRig<MacroUi> r (inst, 1);
    r.refresh();
    const auto tapKey = [&] (int k) { const ui::Rect b = r.ui.keyboard().key (k).r; r.tapAt (b.x + b.w / 2, b.y + b.h / 2); };
    const auto typeText = [&] (const char* text)
    {
        for (const char* c = text; *c != '\0'; ++c)
            tapKey (*c == ' ' ? r.ui.keyboard().keyOfKind (ui::TextEntry::Space) : r.ui.keyboard().keyOf (*c));
    };
    const ui::Rect nameBox = r.ui.view().cards[4].nameRect();
    r.tapAt (nameBox.x + nameBox.w / 2, nameBox.y + nameBox.h / 2);
    r.picture (outDir + "/Named_keyboard_empty.png");
    typeText ("Filter ");
    tapKey (r.ui.keyboard().keyOfKind (ui::TextEntry::ShiftKey));
    typeText ("Sweep");
    r.picture (outDir + "/Named_keyboard_text.png");
    tapKey (r.ui.keyboard().keyOfKind (ui::TextEntry::ShiftKey));
    tapKey (r.ui.keyboard().keyOfKind (ui::TextEntry::ShiftKey));
    typeText ("ab");
    r.picture (outDir + "/Named_keyboard_caps.png");
    tapKey (r.ui.keyboard().keyOfKind (ui::TextEntry::LayerKey));
    typeText ("7+&");
    r.picture (outDir + "/Named_keyboard_symbols.png");
    tapKey (r.ui.keyboard().keyOfKind (ui::TextEntry::LayerKey));
    tapKey (r.ui.keyboard().keyOfKind (ui::TextEntry::Clear));
    typeText ("wwwwwwwwwwwwwwwwww");
    r.picture (outDir + "/Named_keyboard_full.png");
    // a macro that has a name: the field starts with it
    GalleryRig<MacroUi> r2 (inst, 1);
    r2.refresh();
    const ui::Rect nb = r2.ui.view().cards[1].nameRect();
    r2.tapAt (nb.x + nb.w / 2, nb.y + nb.h / 2);
    r2.picture (outDir + "/Named_keyboard_rename.png");
}

int main (int argc, char** argv)
{
    std::string outDir;
    std::vector<std::string> presets;
    std::string onlyPrefix;
    bool testMode = false;
    bool listMode = false;
    int knobs = 16;   // the XL first: the idle chips of the mapped groups read 5 .. 16; `--knobs 4` renders the Key 61 (the chips relative to the bank, 1 .. 4)

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
        else if (arg == "--only-prefix" && i + 1 < argc)
        {
            onlyPrefix = argv[++i];
        }
        else if (arg == "--knobs" && i + 1 < argc)
        {
            knobs = std::atoi (argv[++i]) <= 4 ? 4 : 16;
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
            std::printf ("Usage: %s [--out DIR] [--preset FILE ...] [--only-prefix PREFIX] [--knobs 16|4] [--test] [--list]\n", argv[0]);
            return 0;
        }
    }

    ui::QGroup::setWideDevice (knobs == 16);
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
        renderState (outDir, "Init", initInst, onlyPrefix);
    }

    if (onlyPrefix.empty() || onlyPrefix == "Named" || onlyPrefix.rfind ("Named", 0) == 0)
        renderNames (outDir);

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
            renderState (outDir, stem, inst, onlyPrefix);
        }
        else
        {
            std::fprintf (stderr, "Error loading preset: %s\n", p.c_str());
        }
    }

    std::printf ("ui_gallery_host: finished rendering to %s\n", outDir.c_str());
    return 0;
}
