// SynthyBoy - MPC overlay: UI gallery extra renderer host
// Renders every page, popup and state the standard UI gallery does not render at 1280x800 into PNGs.
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
#include "env_ui.h"
#include "filter_ui.h"
#include "fx_ui.h"
#include "lfo_ui.h"
#include "macro_ui.h"
#include "master_ui.h"
#include "mixer_ui.h"
#include "mod_ui.h"
#include "osc_page.h"
#include "osc_ui.h"
#include "pages.h"
#include "play_view.h"
#include "synthyboy/filter_response.h"
#include "synthyboy/fx_rack.h"
#include "synthyboy/fx_types.h"
#include "synthyboy/osc_cycle.h"
#include "synthyboy/params.h"
#include "synthyboy/preset.h"
#include "synthyboy/state.h"
#include "voice_ui.h"

namespace fs = std::filesystem;
using namespace synthyboy_mpc;
using namespace synthyboy_mpc::overlay;

namespace
{

static std::string g_onlyPrefix;

inline bool shouldRender (const std::string& name)
{
    if (g_onlyPrefix.empty())
        return true;
    return name.rfind (g_onlyPrefix, 0) == 0;
}

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

    void setQVirtual (int id, float v) noexcept override
    {
        if (id >= 0 && id < kVirtualSlots)
            virtualOut[id] = v;
    }

    bool takeQVirtual (int id, float& v) noexcept override
    {
        if (id < 0 || id >= kVirtualSlots || ! virtualDue[id])
            return false;
        virtualDue[id] = false;
        v = virtualIn[id];
        return true;
    }

    std::shared_ptr<const synthyboy::StateContent> content() const override { return content_; }

    void setContent (std::shared_ptr<const synthyboy::StateContent> c) { content_ = std::move (c); }
    /** The wavetable browser's catalogue sources (wavetable_catalogue.h): empty = the built-in tables only. */
    void wavetableSources (synthyboy::CatalogueSources& out) const override { out = wtSources; }
    synthyboy::CatalogueSources wtSources;

    void setLfoShape (int lfo, const synthyboy::ContentLfoShape& shape) override
    {
        auto c = std::make_shared<synthyboy::StateContent>();
        if (content_)
            *c = *content_;
        bool found = false;
        for (auto& s : c->lfoShapes)
        {
            if (s.lfo == lfo)
            {
                s = shape;
                found = true;
                break;
            }
        }
        if (! found)
            c->lfoShapes.push_back (shape);
        content_ = c;
    }

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
    static constexpr int kVirtualSlots = 10;   // QRouter::kNumVirtual
    float virtualOut[kVirtualSlots] = {}, virtualIn[kVirtualSlots] = {};
    bool virtualDue[kVirtualSlots] = {};

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
    const std::string stem = fs::path (path).stem().string();
    if (! shouldRender (stem))
        return;

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

void setupEffectNonDefault (GalleryInst& inst, int type)
{
    switch (type)
    {
        case 14: // splitter low / high
            inst.set ("splitter_lh.on", 1.0f);
            inst.set ("splitter_lh.freq", 800.0f);
            inst.set ("splitter_lh.high_bypass", 1.0f);
            break;
        case 15: // splitter low / mid / high
            inst.set ("splitter_lmh.on", 1.0f);
            inst.set ("splitter_lmh.freq_low", 150.0f);
            inst.set ("splitter_lmh.freq_high", 5000.0f);
            inst.set ("splitter_lmh.mid_bypass", 1.0f);
            break;
        case 16: // splitter mid / side
            inst.set ("splitter_ms.on", 1.0f);
            inst.set ("splitter_ms.side_bypass", 1.0f);
            break;
        case 1: // chorus
            inst.set ("chorus.on", 1.0f);
            inst.set ("chorus.depth", 75.0f);
            inst.set ("chorus.feedback", 55.0f);
            inst.set ("chorus.mix", 65.0f);
            break;
        case 2: // compressor
            inst.set ("compressor.on", 1.0f);
            inst.set ("compressor.threshold", -22.0f);
            inst.set ("compressor.ratio", 4.0f);
            inst.set ("compressor.attack", 15.0f);
            inst.set ("compressor.release", 120.0f);
            inst.set ("compressor.makeup", 6.0f);
            inst.set ("compressor.mix", 100.0f);
            break;
        case 3: // delay
            inst.set ("delay.on", 1.0f);
            inst.set ("delay.feedback", 60.0f);
            inst.set ("delay.mix", 50.0f);
            inst.set ("delay.ping_pong", 1.0f);
            break;
        case 4: // distortion
            inst.set ("distortion.on", 1.0f);
            inst.set ("distortion.type", 1.0f); // Soft clip
            inst.set ("distortion.drive", 14.0f);
            inst.set ("distortion.mix", 85.0f);
            break;
        case 5: // eq
            inst.set ("eq.on", 1.0f);
            inst.set ("eq.low_gain", 4.0f);
            inst.set ("eq.band1_gain", 6.0f);
            inst.set ("eq.band1_cutoff", 800.0f);
            inst.set ("eq.band2_gain", -5.0f);
            inst.set ("eq.band2_cutoff", 2500.0f);
            inst.set ("eq.high_gain", 3.0f);
            break;
        case 6: // filter
            inst.set ("fxfilter.on", 1.0f);
            inst.set ("fxfilter.cutoff", 1800.0f);
            inst.set ("fxfilter.resonance", 65.0f);
            inst.set ("fxfilter.drive", 8.0f);
            inst.set ("fxfilter.mix", 80.0f);
            break;
        case 7: // flanger
            inst.set ("flanger.on", 1.0f);
            inst.set ("flanger.depth", 70.0f);
            inst.set ("flanger.feedback", 65.0f);
            inst.set ("flanger.mix", 60.0f);
            break;
        case 8: // phaser
            inst.set ("phaser.on", 1.0f);
            inst.set ("phaser.feedback", 75.0f);
            inst.set ("phaser.center", 1400.0f);
            inst.set ("phaser.mix", 70.0f);
            break;
        case 9: // reverb
            inst.set ("reverb.on", 1.0f);
            inst.set ("reverb.decay_time", 3.2f);
            inst.set ("reverb.mix", 45.0f);
            inst.set ("reverb.size", 75.0f);
            break;
        case 10: // hyperdimension
            inst.set ("hyper_dimension.on", 1.0f);
            inst.set ("hyper_dimension.dimension_mix", 70.0f);
            inst.set ("hyper_dimension.hyper_mix", 65.0f);
            break;
        case 11: // utility
            inst.set ("utility.on", 1.0f);
            inst.set ("utility.width", 140.0f);
            inst.set ("utility.pan", -15.0f);
            inst.set ("utility.gain", 2.0f);
            break;
        case 12: // bode
            inst.set ("bode.on", 1.0f);
            inst.set ("bode.shift_hz", 180.0f);
            inst.set ("bode.feedback", 40.0f);
            inst.set ("bode.mix", 70.0f);
            break;
        case 13: // convolve
            inst.set ("convolve.on", 1.0f);
            inst.set ("convolve.mix", 65.0f);
            break;
        default:
            break;
    }
}

/** A small wavetable library for the browser states: a pack with two categories and the player's folder with a category and a loose file (the files are placeholders: the browser lists names and sizes only). */
void makeWavetableGalleryLibrary (synthyboy::CatalogueSources& src)
{
    const fs::path root = fs::temp_directory_path() / "synthyboy_gallery_wavetables";
    std::error_code ec;
    fs::remove_all (root, ec);
    const std::string pack = "com.gallery.wavetables";
    fs::create_directories (root / "packs" / pack / "Analog", ec);
    fs::create_directories (root / "packs" / pack / "Vocal", ec);
    const char* const files[][2] = { { "Analog/warm_saw_stack.wav", "Warm Saw Stack" }, { "Analog/thin_pulse_sweep.wav", "Thin Pulse Sweep" }, { "Analog/sync_lead.wav", "Sync Lead" },
                                     { "Vocal/choir_ah.wav", "Choir Ah" }, { "Vocal/formant_sweep.wav", "Formant Sweep" } };
    std::string list;
    for (const auto& f : files)
    {
        std::ofstream (root / "packs" / pack / f[0]) << "placeholder";
        list += std::string (list.empty() ? "" : ",") + "{\"path\":\"" + f[0] + "\",\"sha256\":\"" + std::string (64, 'b') + "\",\"size\":4096,\"title\":\"" + f[1] + "\"}";
    }
    std::ofstream (root / "packs" / pack / "contentpack.json") << "{\"schemaVersion\":1,\"id\":\"" << pack << "\",\"name\":\"Gallery Wavetables\",\"version\":\"0.1.0\",\"license\":\"CC0-1.0\",\"attribution\":\"t\","
                                                                  "\"kind\":\"wavetables\",\"requires_plugin\":\">=0.1.0\",\"files\":[" << list << "]}";
    fs::create_directories (root / "SynthyBoy" / "Wavetables" / "Bass", ec);
    for (const char* n : { "sub_grind.wav", "reese_move.wav" })
        std::ofstream (root / "SynthyBoy" / "Wavetables" / "Bass" / n) << "placeholder";
    std::ofstream (root / "SynthyBoy" / "Wavetables" / "my_loose_table.wav") << "placeholder";
    src.packDirs = { (root / "packs").string() };
    src.userRoots = { root.string() };
}

void renderExtraStates (const std::string& outDir, const std::string& state, GalleryInst& baseInst)
{
    std::printf ("Rendering extra states for: %s ...\n", state.c_str());

    // =========================================================================
    // 1. Oscillator pages (Osc A)
    // =========================================================================
    {
        // 1.1 SAMPLE oscillator type on Osc A - empty
        {
            GalleryRig<OscPageUi> r (baseInst, 2);
            r.inst.set ("osc1.type", 1.0f); // Sample
            r.refresh (2);
            r.picture (outDir + "/" + state + "_osca_sample_empty.png");
        }

        // 1.2 SAMPLE oscillator type on Osc A - content loaded
        {
            GalleryRig<OscPageUi> r (baseInst, 2);
            r.inst.set ("osc1.type", 1.0f); // Sample
            auto c = std::make_shared<synthyboy::StateContent>();
            if (r.inst.content())
                *c = *r.inst.content();
            synthyboy::ContentSample cs;
            cs.osc = 1;
            cs.ref.pack = "user.samples";
            cs.ref.file = "drums/acoustic_snare.wav";
            cs.ref.name = "Acoustic Snare";
            c->samples.push_back (cs);
            r.inst.setContent (c);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_osca_sample_loaded.png");
        }

        // 1.3 GRAIN oscillator type on Osc A - empty
        {
            GalleryRig<OscPageUi> r (baseInst, 2);
            r.inst.set ("osc1.type", 2.0f); // Granular
            r.refresh (2);
            r.picture (outDir + "/" + state + "_osca_grain_empty.png");
        }

        // 1.4 GRAIN oscillator type on Osc A - content loaded
        {
            GalleryRig<OscPageUi> r (baseInst, 2);
            r.inst.set ("osc1.type", 2.0f); // Granular
            auto c = std::make_shared<synthyboy::StateContent>();
            if (r.inst.content())
                *c = *r.inst.content();
            synthyboy::ContentSample cs;
            cs.osc = 1;
            cs.ref.pack = "user.samples";
            cs.ref.file = "pads/analog_choir.wav";
            cs.ref.name = "Analog Choir";
            c->samples.push_back (cs);
            r.inst.setContent (c);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_osca_grain_loaded.png");
        }

        // 1.5 Wavetable browser / list popup
        {
            GalleryRig<OscPageUi> r (baseInst, 2);
            r.inst.set ("osc1.type", 0.0f); // Wavetable
            r.refresh (2);
            r.tap (r.ui.ui().full().basic.source.dropdown);
            r.picture (outDir + "/" + state + "_osca_wavetable_list.png");
        }

        // 1.5b Wavetable browser with a pack and the player's folders: the categories, and a category with the file table that is loaded marked
        {
            synthyboy::CatalogueSources src;
            makeWavetableGalleryLibrary (src);
            GalleryRig<OscPageUi> r (baseInst, 2);
            r.inst.wtSources = src;
            r.inst.set ("osc1.type", 0.0f);
            r.refresh (2);
            r.tap (r.ui.ui().full().basic.source.dropdown);
            const ui::Rect h = r.ui.ui().list().headerRect();
            r.tapAt (h.x + 20, h.y + h.h / 2);   // the way up: the categories
            r.picture (outDir + "/" + state + "_osca_wavetable_categories.png");
            const ui::Rect row = r.ui.ui().list().rowRect (2);
            r.tapAt (row.x + 30, row.y + row.h / 2);   // the third category: the first of the pack's
            r.picture (outDir + "/" + state + "_osca_wavetable_category.png");
        }
        {
            synthyboy::CatalogueSources src;
            makeWavetableGalleryLibrary (src);
            GalleryRig<OscPageUi> r (baseInst, 2);
            r.inst.wtSources = src;
            auto c = std::make_shared<synthyboy::StateContent>();
            if (r.inst.content())
                *c = *r.inst.content();
            synthyboy::ContentWavetable w;
            w.osc = 1;
            w.ref.pack = "com.gallery.wavetables";
            w.ref.file = "Analog/warm_saw_stack.wav";
            w.ref.name = "Warm Saw Stack";
            w.frames = 64;
            c->wavetables.push_back (w);
            r.inst.setContent (c);
            r.inst.set ("osc1.type", 0.0f);
            r.inst.set ("osc1.wavetable", static_cast<float> (synthyboy::kWavetablePresetTable));
            r.refresh (2);
            r.picture (outDir + "/" + state + "_osca_wavetable_file_selector.png");
            r.tap (r.ui.ui().full().basic.source.dropdown);
            r.picture (outDir + "/" + state + "_osca_wavetable_file_marked.png");
        }

        // 1.6 Warp type list popup
        {
            GalleryRig<OscPageUi> r (baseInst, 2);
            r.inst.set ("osc1.type", 0.0f);
            r.refresh (2);
            if (r.ui.ui().full().panel.warp())
                r.tap (r.ui.ui().full().panel.warp()->first.type);
            r.picture (outDir + "/" + state + "_osca_warp_list.png");
        }

        // 1.7 Route / destination popup
        {
            GalleryRig<OscPageUi> r (baseInst, 2);
            r.refresh (2);
            r.tap (r.ui.ui().full().route);
            r.picture (outDir + "/" + state + "_osca_route.png");
        }

        // 1.8 Unison page (MORE)
        {
            GalleryRig<OscPageUi> r (baseInst, 2);
            r.stack.openDetail (OscUi::kPageUnison);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_osca_unison.png");
        }

        // 1.9 Grain window page
        {
            GalleryRig<OscPageUi> r (baseInst, 2);
            r.inst.set ("osc1.type", 2.0f); // Granular
            r.refresh (2);
            r.stack.openDetail (OscUi::kPageGrain);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_osca_grain_window.png");
        }

        // 1.10 Osc modulation list page - zero routes
        {
            GalleryRig<OscPageUi> r (baseInst, 2);
            for (int i = 1; i <= 64; ++i)
            {
                char k[24]; std::snprintf (k, sizeof k, "mod%02d.destination", i);
                if (static_cast<int> (r.inst.get (k)) == static_cast<int> (synthyboy::modDestinationChoiceOf (static_cast<size_t> (GalleryInst::idx ("osc1.level")))))
                    r.inst.set (k, 0.0f);
            }
            r.refresh (2);
            const ui::Rect rc = r.ui.ui().full().basic.level.rect();
            r.tapAt (rc.x + rc.w / 2, rc.y + rc.h / 2, 700);
            if (r.stack.top() != OscUi::kPageMods)
                r.stack.openDetail (OscUi::kPageMods);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_osca_mods_empty.png");
        }

        // 1.11 Osc modulation list page - several routes
        {
            GalleryRig<OscPageUi> r (baseInst, 2);
            r.inst.mod (1, 1, "osc1.level", 40.0f, false);
            r.inst.mod (2, 7, "osc1.level", -50.0f, true);
            r.inst.mod (3, 23, "osc1.level", 30.0f, false);
            const ui::Rect rc = r.ui.ui().full().basic.level.rect();
            r.tapAt (rc.x + rc.w / 2, rc.y + rc.h / 2, 700);
            if (r.stack.top() != OscUi::kPageMods)
                r.stack.openDetail (OscUi::kPageMods);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_osca_mods_routes.png");
        }
    }

    // =========================================================================
    // 2. Filter
    // =========================================================================
    {
        // 2.1 Standard model layout (Digital model)
        {
            GalleryRig<FilterUi> r (baseInst, 1);
            r.inst.set ("filter1.model", 3.0f); // Digital
            r.refresh (2);
            r.picture (outDir + "/" + state + "_filter1_model_standard.png");
        }

        // 2.2 Formant model layout (isFormant mode with XY pad block)
        {
            GalleryRig<FilterUi> r (baseInst, 1);
            r.inst.set ("filter1.model", 5.0f); // Formant
            r.inst.set ("filter1.formant.x", 45.0f);
            r.inst.set ("filter1.formant.y", 75.0f);
            r.inst.set ("filter1.formant.resonance", 60.0f);
            r.inst.set ("filter1.formant.spread", 30.0f);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_filter1_model_formant.png");
        }

        // 2.3 Comb model layout (enables Comb Offset knob)
        {
            GalleryRig<FilterUi> r (baseInst, 1);
            r.inst.set ("filter1.model", 6.0f); // Comb
            r.inst.set ("filter1.comb_offset", 35.0f);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_filter1_model_comb.png");
        }

        // 2.4 Style list popup
        {
            GalleryRig<FilterUi> r (baseInst, 1);
            r.refresh (2);
            r.tap (r.ui.full().style);
            r.picture (outDir + "/" + state + "_filter1_style_list.png");
        }

        // 2.4b Style list popup (classic: Ladder)
        {
            GalleryRig<FilterUi> r (baseInst, 1);
            r.inst.set ("filter1.model", 2.0f); // Ladder (classic)
            r.refresh (2);
            r.tap (r.ui.full().style);
            r.picture (outDir + "/" + state + "_filter1_style_list_classic.png");
        }

        // 2.4c Style list popup (Serum: Scream)
        {
            GalleryRig<FilterUi> r (baseInst, 1);
            r.inst.set ("filter1.model", 16.0f); // Scream (Serum)
            r.refresh (2);
            r.tap (r.ui.full().style);
            r.picture (outDir + "/" + state + "_filter1_style_list_serum.png");
        }

        // 2.5 Output / routing popup
        {
            GalleryRig<FilterUi> r (baseInst, 1);
            r.refresh (2);
            r.tap (r.ui.full().route);
            r.picture (outDir + "/" + state + "_filter1_route.png");
        }

        // 2.6 F1+F2 Parallel routing
        {
            GalleryRig<FilterUi> r (baseInst, 3);
            r.inst.set ("filter.routing", 0.0f); // Parallel
            r.inst.set ("filter1.on", 1.0f); r.inst.set ("filter1.cutoff", 4000.0f); r.inst.set ("filter1.resonance", 50.0f);
            r.inst.set ("filter2.on", 1.0f); r.inst.set ("filter2.cutoff", 800.0f); r.inst.set ("filter2.resonance", 60.0f);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_filter_dual_parallel.png");
        }

        // 2.7 F1+F2 Serial routing
        {
            GalleryRig<FilterUi> r (baseInst, 3);
            r.inst.set ("filter.routing", 1.0f); // Serial
            r.inst.set ("filter1.on", 1.0f); r.inst.set ("filter1.cutoff", 4000.0f); r.inst.set ("filter1.resonance", 50.0f);
            r.inst.set ("filter2.on", 1.0f); r.inst.set ("filter2.cutoff", 800.0f); r.inst.set ("filter2.resonance", 60.0f);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_filter_dual_serial.png");
        }
    }

    // =========================================================================
    // 3. Envelope
    // =========================================================================
    {
        // 3.1 Targets page with zero targets
        {
            GalleryRig<EnvUi> r (baseInst, 1);
            for (int i = 1; i <= 64; ++i)
            {
                char k[24]; std::snprintf (k, sizeof k, "mod%02d.source", i);
                if (static_cast<int> (r.inst.get (k)) == 1)
                    r.inst.set (k, 0.0f);
            }
            r.stack.openDetail (EnvUi::kPageTargets);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_env1_targets_empty.png");
        }

        // 3.2 Targets page with several targets
        {
            GalleryRig<EnvUi> r (baseInst, 1);
            r.inst.mod (1, 1, "filter1.cutoff", 55.0f);
            r.inst.mod (2, 1, "osc1.level", -35.0f);
            r.inst.mod (3, 1, "filter1.resonance", 40.0f);
            r.stack.openDetail (EnvUi::kPageTargets);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_env1_targets_several.png");
        }

        // 3.3 BEATS mode
        {
            GalleryRig<EnvUi> r (baseInst, 1);
            r.inst.set ("env1.units", 1.0f); // Beats mode
            r.refresh (2);
            r.picture (outDir + "/" + state + "_env1_beats.png");
        }

        // 3.4 Envelopes 2-6
        for (int i = 2; i <= 6; ++i)
        {
            GalleryRig<EnvUi> r (baseInst, i);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_env" + std::to_string (i) + ".png");
        }
    }

    // =========================================================================
    // 4. LFO
    // =========================================================================
    {
        // 4.1 BPM mode with controls
        {
            GalleryRig<LfoUi> r (baseInst, 1);
            r.inst.set ("lfo1.sync", 1.0f); // BPM mode
            r.refresh (2);
            r.picture (outDir + "/" + state + "_lfo1_bpm.png");
        }

        // 4.2 KEY mode with controls
        {
            GalleryRig<LfoUi> r (baseInst, 1);
            r.inst.set ("lfo1.sync", 4.0f); // Key sync mode
            r.refresh (2);
            r.picture (outDir + "/" + state + "_lfo1_key.png");
        }

        // 4.3 Random LFO
        {
            GalleryRig<LfoUi> r (baseInst, 1);
            r.refresh (1);
            const ui::Rect rcell = r.ui.full().bar.rndCells.cell (0);
            r.tapAt (rcell.x + 4, rcell.y + 20);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_lfo_random.png");
        }

        // 4.4 Shape DRAWN
        {
            GalleryRig<LfoUi> r (baseInst, 1);
            r.inst.set ("lfo1.shape", 12.0f); // Custom drawn shape
            synthyboy::ContentLfoShape cs;
            cs.lfo = 1;
            cs.points = {
                { 0.0f, 0.1f, 0.0f },
                { 0.25f, 0.85f, 2.0f },
                { 0.5f, 0.15f, -1.8f },
                { 0.75f, 0.9f, 0.0f },
                { 1.0f, 0.0f, 0.0f }
            };
            r.inst.setLfoShape (1, cs);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_lfo1_shape_drawn.png");
        }

        // 4.4b The shape with a rise and a delay (the strip before the cycle) and with a phase of 90 degrees (the marker)
        {
            GalleryRig<LfoUi> r (baseInst, 1);
            r.inst.set ("lfo1.fade_in", 800.0f);
            r.inst.set ("lfo1.delay", 300.0f);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_lfo1_rise_delay.png");
            GalleryRig<LfoUi> r2 (baseInst, 1);
            r2.inst.set ("lfo1.phase", 90.0f);
            r2.refresh (2);
            r2.picture (outDir + "/" + state + "_lfo1_phase90.png");
        }

        // 4.5 Shape editor page - empty (minimum 2 points: start and end)
        {
            GalleryRig<LfoUi> r (baseInst, 1);
            r.inst.set ("lfo1.shape", 12.0f);
            synthyboy::ContentLfoShape cs;
            cs.lfo = 1;
            cs.points = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
            r.inst.setLfoShape (1, cs);
            r.refresh();
            r.tap (r.ui.full().edit);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_lfo_editor_empty.png");
        }

        // 4.6 Shape editor page - a few points
        {
            GalleryRig<LfoUi> r (baseInst, 1);
            r.inst.set ("lfo1.shape", 12.0f);
            synthyboy::ContentLfoShape cs;
            cs.lfo = 1;
            cs.points = { { 0.0f, 1.0f, 0.0f }, { 0.33f, 0.0f, 0.0f }, { 0.66f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
            r.inst.setLfoShape (1, cs);
            r.refresh();
            r.tap (r.ui.full().edit);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_lfo_editor_points.png");
        }

        // 4.7 Shape editor page - Point tool
        {
            GalleryRig<LfoUi> r (baseInst, 1);
            r.refresh();
            r.tap (r.ui.full().edit);
            const ui::Rect c = r.ui.editor().tools.cell (ui::ToolSelector::Point);
            r.tapAt (c.x + c.w / 2, c.y + c.h / 2);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_lfo_editor_tool_point.png");
        }

        // 4.8 Shape editor page - Flat tool
        {
            GalleryRig<LfoUi> r (baseInst, 1);
            r.refresh();
            r.tap (r.ui.full().edit);
            const ui::Rect c = r.ui.editor().tools.cell (ui::ToolSelector::Flat);
            r.tapAt (c.x + c.w / 2, c.y + c.h / 2);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_lfo_editor_tool_flat.png");
        }

        // 4.9 Shape editor page - RampUp tool
        {
            GalleryRig<LfoUi> r (baseInst, 1);
            r.refresh();
            r.tap (r.ui.full().edit);
            const ui::Rect c = r.ui.editor().tools.cell (ui::ToolSelector::RampUp);
            r.tapAt (c.x + c.w / 2, c.y + c.h / 2);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_lfo_editor_tool_ramp_up.png");
        }

        // 4.10 Shape editor page - RampDown tool
        {
            GalleryRig<LfoUi> r (baseInst, 1);
            r.refresh();
            r.tap (r.ui.full().edit);
            const ui::Rect c = r.ui.editor().tools.cell (ui::ToolSelector::RampDown);
            r.tapAt (c.x + c.w / 2, c.y + c.h / 2);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_lfo_editor_tool_ramp_down.png");
        }

        // 4.11 .. 4.30 The LFO editor, the new tools, the marker, the selection, the lists, the name keyboard and the page of the modulation of points
        {
            const std::string prefix = outDir + "/" + state + "_lfo_editor_";
            auto openEd = [&] (float sync, float phaseDeg, bool modulated) -> std::unique_ptr<GalleryRig<LfoUi>>
            {
                auto r = std::make_unique<GalleryRig<LfoUi>> (baseInst, 1);
                r->inst.set ("lfo1.shape", 12.0f);
                r->inst.set ("lfo1.sync_type", sync);
                r->inst.set ("lfo1.phase", phaseDeg);
                synthyboy::ContentLfoShape cs;
                cs.lfo = 1;
                cs.gridX = 8;
                cs.gridY = 4;
                cs.points = { { 0.0f, 0.1f, 0.0f }, { 0.125f, 0.85f, 2.0f }, { 0.25f, 0.3f, 0.0f }, { 0.375f, 0.9f, -1.8f }, { 0.5f, 0.15f, 0.0f }, { 0.625f, 0.7f, 3.0f }, { 0.75f, 0.4f, 0.0f }, { 0.875f, 0.8f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
                if (modulated)
                {
                    cs.points[2].modY.present = true; cs.points[2].modY.source = synthyboy::modSourceTable()[7].key; cs.points[2].modY.amount = 0.3f;
                    cs.points[5].modX.present = true; cs.points[5].modX.source = synthyboy::modSourceTable()[7].key; cs.points[5].modX.amount = 0.06f;
                    cs.points[1].modCurve.present = true; cs.points[1].modCurve.source = synthyboy::modSourceTable()[7].key; cs.points[1].modCurve.amount = 0.4f;
                }
                r->inst.setLfoShape (1, cs);
                r->refresh();
                r->tap (r->ui.full().edit);
                r->refresh (2);
                return r;
            };
            auto px = [] (GalleryRig<LfoUi>& r, float f) { return static_cast<int> (r.ui.editor().canvas.xOf (f)); };
            auto py = [] (GalleryRig<LfoUi>& r, float f) { return static_cast<int> (r.ui.editor().canvas.yOf (f)); };
            auto toolOf = [] (GalleryRig<LfoUi>& r, int t) { const ui::Rect c = r.ui.editor().tools.cell (t); r.tapAt (c.x + c.w / 2, c.y + c.h / 2); };
            auto touch = [] (GalleryRig<LfoUi>& r, TouchEvent::Kind k, int x, int y)
            {
                TouchEvent e; e.kind = k; e.x = x; e.y = y;
                r.now += 16.0;
                r.ui.touch (e, r.stack, r.inst, r.now);
                r.refresh (1);
            };
            auto dragFrom = [&] (GalleryRig<LfoUi>& r, int x0, int y0, int x1, int y1)   // the finger stays down: the picture shows the gesture in flight
            {
                touch (r, TouchEvent::Down, x0, y0);
                for (int i = 1; i <= 8; ++i)
                    touch (r, TouchEvent::Move, x0 + (x1 - x0) * i / 8, y0 + (y1 - y0) * i / 8);
            };
            // the marker: LOOP (a loop mode), START (retrigger), dimmed with its hint (free-run)
            openEd (4.0f, 135.0f, false)->picture (prefix + "marker_loop.png");
            openEd (0.0f, 45.0f, false)->picture (prefix + "marker_start.png");
            openEd (1.0f, 45.0f, false)->picture (prefix + "marker_freerun.png");
            // the modulation of points: the bars and the ring of a curve handle
            openEd (0.0f, 0.0f, true)->picture (prefix + "mod_bars.png");
            // SELECT: the rubber band in flight, the selection done (the POINT field shows the count, the third field is SCALE)
            {
                auto r = openEd (0.0f, 0.0f, false);
                toolOf (*r, ui::ToolSelector::Select);
                dragFrom (*r, px (*r, 0.18f), py (*r, 0.98f), px (*r, 0.55f), py (*r, 0.05f));
                r->picture (prefix + "select_rubber.png");
                touch (*r, TouchEvent::Up, px (*r, 0.55f), py (*r, 0.05f));
                r->refresh (2);
                r->picture (prefix + "select_done.png");
            }
            // SOFT: the rainbow of the points that follow the finger
            {
                auto r = openEd (0.0f, 0.0f, false);
                toolOf (*r, ui::ToolSelector::Soft);
                dragFrom (*r, px (*r, 0.375f), py (*r, 0.9f), px (*r, 0.375f), py (*r, 0.55f));
                r->picture (prefix + "soft.png");
            }
            // BEND: every curve moves with the one that is dragged
            {
                auto r = openEd (0.0f, 0.0f, false);
                toolOf (*r, ui::ToolSelector::Bend);
                float hx = 0.0f, hy = 0.0f;
                r->ui.editor().canvas.midHandle (0, hx, hy);
                dragFrom (*r, static_cast<int> (hx), static_cast<int> (hy), static_cast<int> (hx), static_cast<int> (hy) + 50);
                r->picture (prefix + "bend.png");
            }
            // the lists of SHAPES and MORE over the editor
            {
                auto r = openEd (0.0f, 0.0f, false);
                r->tap (r->ui.editor().shapes);
                r->picture (prefix + "shapes_list.png");
                r->tapAt (r->ui.list().rowRect (0).x + 20, r->ui.list().rowRect (0).y + 20);
                r->picture (prefix + "shapes_factory.png");
            }
            {
                auto r = openEd (0.0f, 0.0f, false);
                r->tap (r->ui.editor().more);
                r->picture (prefix + "more_list.png");
                r->tapAt (r->ui.list().rowRect (0).x + 20, r->ui.list().rowRect (0).y + 20);
                r->picture (prefix + "more_edit.png");
                r->stack.back();
                r->refresh();
                r->tap (r->ui.editor().more);
                r->tapAt (r->ui.list().rowRect (2).x + 20, r->ui.list().rowRect (2).y + 20);
                r->picture (prefix + "more_copy.png");
                r->tapAt (r->ui.list().rowRect (0).x + 20, r->ui.list().rowRect (0).y + 20);
                r->picture (prefix + "more_copy_lfo.png");
            }
            // SAVE AS: the name keyboard, then the question when the name is taken (a temporary card with one saved shape)
            {
                const std::string card = (fs::temp_directory_path() / "synthyboy_lfo_gallery_card").string();
                std::error_code ec;
                fs::remove_all (card, ec);
                fs::create_directories (card, ec);
                auto r = openEd (0.0f, 0.0f, false);
                r->inst.wtSources.userRoots = { card };
                auto typeName = [&] (const std::string& name)
                {
                    for (char ch : name)
                    {
                        const int k = ch == ' ' ? r->ui.keyboard().keyOfKind (ui::TextEntry::Space) : r->ui.keyboard().keyOf (ch);
                        const ui::TextEntry::Key& key = r->ui.keyboard().key (k);
                        r->tapAt (key.r.x + key.r.w / 2, key.r.y + key.r.h / 2);
                    }
                };
                r->tap (r->ui.editor().shapes);
                r->tapAt (r->ui.list().rowRect (2).x + 20, r->ui.list().rowRect (2).y + 20);
                typeName ("Wobble");
                r->picture (prefix + "save_name.png");
                const ui::TextEntry::Key& ok = r->ui.keyboard().key (r->ui.keyboard().keyOfKind (ui::TextEntry::Ok));
                r->tapAt (ok.r.x + ok.r.w / 2, ok.r.y + ok.r.h / 2);
                r->picture (prefix + "saved.png");
                r->tap (r->ui.editor().shapes);
                r->picture (prefix + "shapes_list_saved.png");
                r->tapAt (r->ui.list().rowRect (1).x + 20, r->ui.list().rowRect (1).y + 20);
                r->picture (prefix + "shapes_mine.png");
                r->stack.back();
                r->refresh();
                r->tap (r->ui.editor().shapes);
                r->tapAt (r->ui.list().rowRect (2).x + 20, r->ui.list().rowRect (2).y + 20);
                typeName ("Wobble");
                const ui::TextEntry::Key& ok2 = r->ui.keyboard().key (r->ui.keyboard().keyOfKind (ui::TextEntry::Ok));
                r->tapAt (ok2.r.x + ok2.r.w / 2, ok2.r.y + ok2.r.h / 2);
                r->picture (prefix + "confirm_replace.png");
                r->stack.back();
                r->refresh();
                r->tap (r->ui.editor().shapes);
                r->tapAt (r->ui.list().rowRect (5).x + 20, r->ui.list().rowRect (5).y + 20);
                r->picture (prefix + "delete_which.png");
                fs::remove_all (card, ec);
            }
            // the modulation of a point: nothing yet, then a source and an amount
            {
                auto r = openEd (0.0f, 0.0f, false);
                r->tapAt (px (*r, 0.25f), py (*r, 0.3f));
                r->tap (r->ui.editor().modButton);
                r->picture (prefix + "pointmod_empty.png");
                const ui::Rect chip = r->ui.pointModPage().board.chipRectOfId (8);
                r->tapAt (chip.x + chip.w / 2, chip.y + chip.h / 2);
                r->picture (prefix + "pointmod.png");
            }
        }
    }

    // =========================================================================
    // 5. Mod matrix
    // =========================================================================
    {
        // 5.1 Slot detail page (MORE) with AUX / SHAPE / TIME blocks
        {
            GalleryRig<ModUi> r (baseInst, 1);
            r.inst.mod (1, 7, "filter1.cutoff", 50.0f);
            r.refresh (2);
            r.tap (r.ui.matrix().windowRows[0].detailBtn);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_mod_slot_detail.png");
        }

        // 5.2 Source picker groups (top level)
        {
            GalleryRig<ModUi> r (baseInst, 1);
            r.inst.mod (1, 7, "filter1.cutoff", 50.0f);
            r.refresh (2);
            r.tap (r.ui.matrix().windowRows[0].sourceBtn);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_mod_source_picker_groups.png");
        }

        // 5.3 Source picker groups expanded (1..5)
        for (int g = 1; g <= uibind::kSourceGroups; ++g)
        {
            GalleryRig<ModUi> r (baseInst, 1);
            r.inst.mod (1, 7, "filter1.cutoff", 50.0f);
            r.refresh (2);
            r.tap (r.ui.matrix().windowRows[0].sourceBtn);
            const ui::Rect row = r.ui.list().rowRect (g);
            r.tapAt (row.x + row.w / 2, row.y + row.h / 2);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_mod_source_picker_group" + std::to_string (g) + ".png");
        }

        // 5.4 Target picker groups (top level)
        {
            GalleryRig<ModUi> r (baseInst, 1);
            r.inst.mod (1, 7, "filter1.cutoff", 50.0f);
            r.refresh (2);
            r.tap (r.ui.matrix().windowRows[0].destBtn);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_mod_target_picker_groups.png");
        }

        // 5.5 Target picker groups expanded (every group in DestIndex)
        const uibind::DestIndex& dIndex = uibind::DestIndex::get();
        for (size_t g = 0; g < dIndex.groups.size(); ++g)
        {
            GalleryRig<ModUi> r (baseInst, 1);
            r.inst.mod (1, 7, "filter1.cutoff", 50.0f);
            r.refresh (2);
            r.tap (r.ui.matrix().windowRows[0].destBtn);
            const ui::Rect row = r.ui.list().rowRect (static_cast<int> (1 + g));
            r.tapAt (row.x + row.w / 2, row.y + row.h / 2);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_mod_target_picker_group" + std::to_string (g) + ".png");
        }

        // 5.6 BY SOURCE view
        {
            GalleryRig<ModUi> r (baseInst, 1);
            r.inst.mod (1, 7, "filter1.cutoff", 60.0f);
            r.inst.mod (2, 7, "filter1.resonance", -30.0f);
            r.inst.mod (3, 1, "osc1.tune", 25.0f);
            r.refresh (2);
            r.tap (r.ui.matrix().tabSource);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_mod_by_source.png");
        }

        // 5.7 BY DEST view
        {
            GalleryRig<ModUi> r (baseInst, 1);
            r.inst.mod (1, 7, "filter1.cutoff", 60.0f);
            r.inst.mod (2, 7, "filter1.resonance", -30.0f);
            r.inst.mod (3, 1, "osc1.tune", 25.0f);
            r.refresh (2);
            r.tap (r.ui.matrix().tabDest);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_mod_by_dest.png");
        }

        // 5.8 Full matrix that scrolls
        {
            GalleryRig<ModUi> r (baseInst, 1);
            for (int i = 1; i <= 16; ++i)
                r.inst.mod (i, (i % 6) + 1, (i % 2 == 0 ? "filter1.cutoff" : "osc1.level"), static_cast<float> (i * 5 - 40));
            r.refresh (2);
            r.ui.wheel (600, 300, -4, r.stack, r.inst, r.now);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_mod_matrix_scrolled.png");
        }
    }

    // =========================================================================
    // 6. Macros
    // =========================================================================
    {
        // 6.1 Macro targets page
        {
            GalleryRig<MacroUi> r (baseInst, 1);
            r.inst.mod (1, 23, "filter1.cutoff", 50.0f);
            r.inst.mod (2, 23, "filter1.resonance", -35.0f);
            r.inst.mod (3, 23, "master.volume", 20.0f);
            r.refresh (2);
            const ui::Rect rc = r.ui.view().cards[0].chipsRect();
            r.tapAt (rc.x + rc.w / 2, rc.y + rc.h / 2);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_macro_targets.png");
        }

        // 6.2 Bank 2
        {
            GalleryRig<MacroUi> r (baseInst, 1);
            r.refresh (2);
            r.tapAt (r.ui.view().bankList.rowRect (1).x + 30, r.ui.view().bankList.rowRect (1).y + 28);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_macros_bank2.png");
        }
    }

    // =========================================================================
    // 7. Mixer
    // =========================================================================
    {
        // 7.1 Each strip selected once (routing line drawn)
        static const char* const kStripNames[10] = {
            "sub", "osca", "oscb", "oscc", "noise", "filter1", "filter2", "bus1", "bus2", "master"
        };
        for (int s = 0; s < 10; ++s)
        {
            GalleryRig<MixerUi> r (baseInst, 1);
            r.refresh (2);
            r.tap (r.ui.screen().strips[s]);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_mixer_strip_" + kStripNames[s] + ".png");
        }

        // 7.2 Destination popup
        {
            GalleryRig<MixerUi> r (baseInst, 1);
            r.refresh (2);
            r.tap (r.ui.screen().strips[1].destButton); // Osc A strip destination
            r.picture (outDir + "/" + state + "_mixer_dest_popup.png");
        }
    }

    // =========================================================================
    // 8. FX
    // =========================================================================
    {
        // 8.1 Every effect type on each page with effect switched on & non-default values
        static const char* const kFxNames[17] = {
            "", "chorus", "compressor", "delay", "distortion", "eq", "filter",
            "flanger", "phaser", "reverb", "hyperdimension", "utility", "bode", "convolve",
            "splitter_lh", "splitter_lmh", "splitter_ms"   // the three splitters: modules of the fixed chain, added from the same list
        };
        for (int t = 1; t <= 16; ++t)
        {
            GalleryRig<FxUi> r (baseInst, 1);
            setupEffectNonDefault (r.inst, t);
            r.refresh();
            addEffect (r, t);
            const int numPages = r.ui.screen().slotEditor.numPages();
            for (int p = 0; p < numPages; ++p)
            {
                r.ui.screen().slotEditor.setPage (p);
                r.refresh (2);
                std::string fname = state + "_fx_" + kFxNames[t];
                if (numPages > 1)
                    r.picture (outDir + "/" + fname + "_page" + std::to_string (p + 1) + ".png");
                else
                    r.picture (outDir + "/" + fname + ".png");
            }
        }

        // 8.2 Rack selector on BUS 1
        {
            GalleryRig<FxUi> r (baseInst, 1);
            setupEffectNonDefault (r.inst, 4);
            r.refresh();
            addEffect (r, 4); // Distortion
            const ui::Rect rk = r.ui.screen().slotEditor.rack.rect();
            r.tapAt (rk.x + rk.w * 3 / 4, rk.y + rk.h / 2); // step to Bus 1
            r.refresh (2);
            r.picture (outDir + "/" + state + "_fx_rack_bus1.png");
        }

        // 8.3 Rack selector on BUS 2
        {
            GalleryRig<FxUi> r (baseInst, 1);
            setupEffectNonDefault (r.inst, 4);
            r.refresh();
            addEffect (r, 4); // Distortion
            const ui::Rect rk = r.ui.screen().slotEditor.rack.rect();
            r.tapAt (rk.x + rk.w * 3 / 4, rk.y + rk.h / 2); // step to Bus 1
            r.tapAt (rk.x + rk.w * 3 / 4, rk.y + rk.h / 2); // step to Bus 2
            r.refresh (2);
            r.picture (outDir + "/" + state + "_fx_rack_bus2.png");
        }

        // 8.4 Chain with nine effects (bar full)
        {
            GalleryRig<FxUi> r (baseInst, 1);
            r.refresh();
            for (int t = 1; t <= 9; ++t)
                addEffect (r, t);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_fx_chain_full.png");
        }

        // 8.5 Gallery when chain has effects (ADD pressed)
        {
            GalleryRig<FxUi> r (baseInst, 1);
            r.refresh();
            addEffect (r, 1); // Chorus
            addEffect (r, 3); // Delay
            r.tap (r.ui.screen().chainBar.plusCard);
            r.picture (outDir + "/" + state + "_fx_gallery_add.png");
        }

        // 8.6 Effect parameter choice list popup in slot editor
        {
            GalleryRig<FxUi> r (baseInst, 1);
            setupEffectNonDefault (r.inst, 4);
            r.refresh();
            addEffect (r, 4); // Distortion
            r.tap (r.ui.screen().slotEditor.steppers[0]); // Distortion type stepper dropdown
            r.picture (outDir + "/" + state + "_fx_param_choice_popup.png");
        }
    }

    // =========================================================================
    // 9. Master
    // =========================================================================
    {
        // 9.1 Main side with non-default values
        {
            GalleryRig<MasterUi> r (baseInst, 1);
            r.inst.set ("master.volume", 0.85f);
            r.inst.set ("master.pan", 25.0f);
            r.inst.set ("master.limiter", 1.0f);
            r.inst.set ("master.voice_limit", 8.0f);
            r.ui.screen().setSide (ui::MasterScreen::SideMain);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_master_main.png");
        }

        // 9.1b Main side with PAD PRESS switch on
        {
            GalleryRig<MasterUi> r (baseInst, 1);
            r.inst.setPadPressure (true, 50.0f);
            r.inst.set ("master.volume", 0.85f);
            r.inst.set ("master.pan", 25.0f);
            r.inst.set ("master.limiter", 1.0f);
            r.inst.set ("master.voice_limit", 8.0f);
            r.ui.screen().setSide (ui::MasterScreen::SideMain);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_master_pad_press.png");
        }

        // 9.2 Tuning/MPE side with non-default values
        {
            GalleryRig<MasterUi> r (baseInst, 1);
            r.inst.set ("master.bend_up", 12.0f);
            r.inst.set ("master.bend_down", -12.0f);
            r.inst.set ("master.mpe", 1.0f);
            r.inst.set ("master.tune_base", 432.0f);
            r.ui.screen().setSide (ui::MasterScreen::SideTuning);
            r.refresh (2);
            r.picture (outDir + "/" + state + "_master_tuning_mpe.png");
        }
    }

    // =========================================================================
    // 10. Voice
    // =========================================================================
    {
        // Sequence with diverse non-default values per step and each row selected
        static const char* const kVoiceRows[6] = { "pan", "detune", "cutoff", "env", "mod1", "mod2" };
        static const float kStepVals[8] = { -0.7f, 0.4f, 0.85f, -0.3f, 0.6f, -0.9f, 0.2f, -0.5f };

        for (int r = 0; r < 6; ++r)
        {
            GalleryRig<VoiceUi> rVoice (baseInst, 1);
            for (int s = 0; s < 8; ++s)
            {
                std::string paramKey = "voicectl.s" + std::to_string (s + 1) + "." + kVoiceRows[r];
                rVoice.inst.set (paramKey, kStepVals[(s + r) % 8] * 100.0f);
            }
            rVoice.refresh (2);
            const ui::Rect tr = rVoice.ui.screen().rowList.rowRect (r);   // (the row list of the page)
            rVoice.tapAt (tr.x + tr.w / 2, tr.y + tr.h / 2);
            rVoice.ui.screen().grid.state.selectedStep = r;
            rVoice.refresh (2);
            rVoice.picture (outDir + "/" + state + "_voice_row_" + kVoiceRows[r] + ".png");
        }
    }

    // =========================================================================
    // 11. UI Component Library Popups
    // =========================================================================
    {
        // 11.1 Generic ListPopup over screen
        {
            saveScene (outDir + "/" + state + "_lib_list_popup.png", [] (gpu::SceneBuilder& sb)
            {
                static const char* const kItems[] = { "SAW", "PULSE", "TRIANGLE", "SINE", "SQUARE", "ORGAN", "VOCAL", "METALLIC" };
                ui::ListPopup p;
                p.state.items = kItems;
                p.state.count = 8;
                p.state.current = 2;
                p.state.header = "WAVEFORM SELECT";
                p.state.headerBack = true;
                p.setPopup ({ 0, 0, 1280, 800 });
                p.place ({ 440, 160, 400, 480 });
                p.draw (sb);
            });
        }

        // 11.2 Generic RoutePanel popup over screen
        {
            saveScene (outDir + "/" + state + "_lib_route_panel.png", [] (gpu::SceneBuilder& sb)
            {
                ui::RoutePanel rp;
                rp.setPopup (true, { 0, 0, 1280, 800 });
                rp.place ({ 480, 220, 320, 360 });
                rp.draw (sb);
            });
        }
    }
}

} // namespace

int main (int argc, char** argv)
{
    ui::QGroup::setWideDevice (true);   // the XL first, as the main gallery host: the idle chips of the mapped groups read 5 .. 16
    std::string outDir;
    std::vector<std::string> presets;
    bool testMode = false;

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
            g_onlyPrefix = argv[++i];
        }
        else if (arg == "--test")
        {
            testMode = true;
        }
        else if (arg == "--help" || arg == "-h")
        {
            std::printf ("Usage: %s [--out DIR] [--preset FILE ...] [--presets FILE ...] [--only-prefix PFX] [--test]\n", argv[0]);
            return 0;
        }
    }

    if (testMode)
    {
        std::printf ("Running ui_gallery_extra_host test...\n");
        std::string tmpDir = "/tmp/synthyboy_ui_gallery_extra_test";
        fs::create_directories (tmpDir);
        GalleryInst inst;
        renderExtraStates (tmpDir, "test_init", inst);

        // Verify key images exist and are not empty
        std::vector<std::string> expected = {
            "test_init_osca_sample_empty.png",
            "test_init_osca_sample_loaded.png",
            "test_init_osca_grain_empty.png",
            "test_init_osca_grain_loaded.png",
            "test_init_osca_wavetable_list.png",
            "test_init_osca_wavetable_categories.png",
            "test_init_osca_wavetable_category.png",
            "test_init_osca_wavetable_file_selector.png",
            "test_init_osca_wavetable_file_marked.png",
            "test_init_osca_warp_list.png",
            "test_init_osca_route.png",
            "test_init_osca_unison.png",
            "test_init_osca_grain_window.png",
            "test_init_osca_mods_empty.png",
            "test_init_osca_mods_routes.png",
            "test_init_filter1_model_standard.png",
            "test_init_filter1_model_formant.png",
            "test_init_filter1_model_comb.png",
            "test_init_filter1_style_list.png",
            "test_init_filter1_style_list_classic.png",
            "test_init_filter1_style_list_serum.png",
            "test_init_filter1_route.png",
            "test_init_filter_dual_parallel.png",
            "test_init_filter_dual_serial.png",
            "test_init_env1_targets_empty.png",
            "test_init_env1_targets_several.png",
            "test_init_env1_beats.png",
            "test_init_env2.png",
            "test_init_env3.png",
            "test_init_env4.png",
            "test_init_env5.png",
            "test_init_env6.png",
            "test_init_lfo1_bpm.png",
            "test_init_lfo1_key.png",
            "test_init_lfo_random.png",
            "test_init_lfo1_shape_drawn.png",
            "test_init_lfo1_rise_delay.png",
            "test_init_lfo1_phase90.png",
            "test_init_lfo_editor_empty.png",
            "test_init_lfo_editor_points.png",
            "test_init_lfo_editor_tool_point.png",
            "test_init_lfo_editor_tool_flat.png",
            "test_init_lfo_editor_tool_ramp_up.png",
            "test_init_lfo_editor_tool_ramp_down.png",
            "test_init_lfo_editor_marker_loop.png",
            "test_init_lfo_editor_marker_start.png",
            "test_init_lfo_editor_marker_freerun.png",
            "test_init_lfo_editor_mod_bars.png",
            "test_init_lfo_editor_select_rubber.png",
            "test_init_lfo_editor_select_done.png",
            "test_init_lfo_editor_soft.png",
            "test_init_lfo_editor_bend.png",
            "test_init_lfo_editor_shapes_list.png",
            "test_init_lfo_editor_shapes_factory.png",
            "test_init_lfo_editor_more_list.png",
            "test_init_lfo_editor_more_edit.png",
            "test_init_lfo_editor_more_copy.png",
            "test_init_lfo_editor_more_copy_lfo.png",
            "test_init_lfo_editor_save_name.png",
            "test_init_lfo_editor_saved.png",
            "test_init_lfo_editor_shapes_list_saved.png",
            "test_init_lfo_editor_shapes_mine.png",
            "test_init_lfo_editor_confirm_replace.png",
            "test_init_lfo_editor_delete_which.png",
            "test_init_lfo_editor_pointmod_empty.png",
            "test_init_lfo_editor_pointmod.png",
            "test_init_mod_slot_detail.png",
            "test_init_mod_source_picker_groups.png",
            "test_init_mod_source_picker_group1.png",
            "test_init_mod_target_picker_groups.png",
            "test_init_mod_target_picker_group0.png",
            "test_init_mod_by_source.png",
            "test_init_mod_by_dest.png",
            "test_init_mod_matrix_scrolled.png",
            "test_init_macro_targets.png",
            "test_init_macros_bank2.png",
            "test_init_mixer_strip_sub.png",
            "test_init_mixer_strip_osca.png",
            "test_init_mixer_dest_popup.png",
            "test_init_fx_chorus.png",
            "test_init_fx_compressor_page1.png",
            "test_init_fx_compressor_page2.png",
            "test_init_fx_rack_bus1.png",
            "test_init_fx_rack_bus2.png",
            "test_init_fx_chain_full.png",
            "test_init_fx_gallery_add.png",
            "test_init_fx_param_choice_popup.png",
            "test_init_master_main.png",
            "test_init_master_pad_press.png",
            "test_init_master_tuning_mpe.png",
            "test_init_voice_row_pan.png",
            "test_init_voice_row_detune.png",
            "test_init_lib_list_popup.png",
            "test_init_lib_route_panel.png"
        };

        bool ok = true;
        for (const auto& item : expected)
        {
            std::string path = tmpDir + "/" + item;
            if (! fs::exists (path) || fs::file_size (path) < 1000)
            {
                std::printf ("FAIL: Missing or empty test image: %s\n", item.c_str());
                ok = false;
            }
        }
        fs::remove_all (tmpDir);
        if (ok)
        {
            std::printf ("PASS  ui_gallery_extra_host self-test: all expected images rendered successfully\n");
            std::printf ("ui_gallery_extra_host: OK\n");
            return 0;
        }
        return 1;
    }

    if (outDir.empty())
        outDir = "build/ui-gallery-extra";

    fs::create_directories (outDir);

    // Render Init state
    {
        GalleryInst initInst;
        renderExtraStates (outDir, "Init", initInst);
    }

    // Default preset if none specified
    if (presets.empty())
    {
        std::vector<std::string> defaults = {
            "content/presets/Bass/scream_filter_bass.sbpreset"
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
            renderExtraStates (outDir, stem, inst);
        }
        else
        {
            std::fprintf (stderr, "Error loading preset: %s\n", p.c_str());
        }
    }

    std::printf ("ui_gallery_extra_host: finished rendering to %s\n", outDir.c_str());
    return 0;
}
