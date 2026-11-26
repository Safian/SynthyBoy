// SynthyBoy - MPC overlay tests: the pages of the OSC tab (osc_page.h): one page per oscillator (A, B, C, Sub, Noise) built on the full oscillator UI, with the oscillator's display; the page names its
// oscillator itself (the Play page's selector is neither read nor written), has no CLOSE button, sets the Q groups of the full view and opens the detail pages of the oscillator UI.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <string>
#include <vector>

#include "synthyboy/user_content.h"
#include "../osc_page.h"
#include "../scene_cpu.h"

using namespace synthyboy_mpc;
using namespace synthyboy_mpc::overlay;

namespace
{
int g_fails = 0;
void check (bool ok, const std::string& what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok)
        ++g_fails;
}

/** A plugin instance made of an array of plain values: writeParam converts like the plugin does and counts the writes (the host notification). */
class Inst final : public IInstanceView
{
public:
    Inst()
    {
        const synthyboy::ParamInfo* t = synthyboy::paramTable();
        for (size_t i = 0; i < synthyboy::paramCount(); ++i)
            v_[i] = t[i].def;
    }
    float param (int i) const noexcept override { return v_[i]; }
    bool live (synthyboy::LiveValues&) const noexcept override { return false; }
    double sampleRate() const noexcept override { return 48000.0; }
    int playSelection (int) const noexcept override { return sel; }
    void setQGroup (const int* t, int n) noexcept override { for (int i = 0; i < 4; ++i) q[i] = i < n ? t[i] : -1; ++qCalls; }
    void writeParam (int i, float norm) noexcept override
    {
        const synthyboy::ParamInfo& p = synthyboy::paramTable()[i];
        v_[i] = synthyboy::constrainValue (p, synthyboy::fromNormalised (p, norm));
        ++writes;
        lastWritten = i;
    }
    void setPlaySelection (int g, int i) noexcept override { if (g == 0) sel = i; ++selWrites; }
    int playLastSelector() const noexcept override { return lastSel; }
    void setUiFullOpen (bool o) noexcept override { fullOpen = o; }
    uint64_t stateGeneration() const noexcept override { return generation; }
    void setNativeOscState (const nativeui::osc::ViewState& state) noexcept override
    {
        native.publish (state);
        native.report (native.snapshot());
    }
    bool takeNativeOsc (const nativeui::osc::ViewState& state, int field, int& choice) noexcept override
    {
        return native.take (state, field, choice);
    }
    bool nativeOscSettingsSupported() const noexcept override { return useNativeSettings; }
    void setNativeOscSettings (int view, const nativeui::FxCell* fields) noexcept override
    {
        settings.publish (view, fields);
        for (int i = 0; i < nativeui::osc::kSettingsAll; ++i)
            settings.report (settings.at (i));
    }
    bool takeNativeOscSetting (int field, const nativeui::FxCell& expected, float& value) noexcept override
    {
        return settings.take (field, expected, value);
    }
    void postSetting (int field, float value) noexcept { settings.post (settings.at (field), value); }
    bool nativeOscDetailSupported() const noexcept override { return useNativeDetail; }
    void setNativeOscDetail (const nativeui::oscdetail::State& s) noexcept override
    {
        detail.publish (s);
        for (int i = 0; i < nativeui::oscdetail::kBindings; ++i)
            detail.report (detail.at (i));
    }
    bool takeNativeOscDetail (const nativeui::oscdetail::State& s, int field, float& value) noexcept override
    {
        return detail.take (s, field, value);
    }
    void setNativeOscDetailMenu (int menu) noexcept override { detail.setMenu (menu); }
    void postDetail (int field, float value = 1.0f) noexcept { detail.post (detail.at (field), value, generation); }
    void contentRoots (std::vector<ContentRoot>& out) const override { out = roots; }
    void requestContent (int osc, int kind, const std::string& path) override { requests.push_back ({ osc, kind, path }); loadState = 1; }
    int contentLoad (std::string* m) const override { if (m != nullptr) *m = loadMessage; return loadState; }
    void wavetableSources (synthyboy::CatalogueSources& out) const override { out = sources; }
    void requestWavetable (int osc, const synthyboy::ContentFileRef& ref) override { wtRequests.push_back ({ osc, ref }); loadState = 1; }
    int takeWavetableSteps (int) noexcept override { const int n = wtSteps; wtSteps = 0; return n; }
    std::shared_ptr<const synthyboy::StateContent> content() const override { return contentPtr; }
    struct WtRequest { int osc; synthyboy::ContentFileRef ref; };
    std::vector<WtRequest> wtRequests;
    synthyboy::CatalogueSources sources;
    std::string loadMessage;
    int wtSteps = 0;
    struct Request { int osc, kind; std::string path; };
    std::vector<ContentRoot> roots;
    std::vector<Request> requests;
    int loadState = 0;
    std::shared_ptr<const synthyboy::StateContent> contentPtr;
    const synthyboy::IContentStore* store = nullptr;
    const synthyboy::IContentStore* contentStore() const noexcept override { return store; }
    static int idx (const std::string& key) { const auto* p = synthyboy::findParamByKey (key.c_str()); return p ? static_cast<int> (p - synthyboy::paramTable()) : -1; }
    void set (const std::string& key, float v) { v_[idx (key)] = v; }
    float get (const std::string& key) const { return v_[idx (key)]; }
    int sel = 2, lastSel = 0, q[4] = { -1, -1, -1, -1 }, qCalls = 0, writes = 0, lastWritten = -1, selWrites = 0;
    bool fullOpen = false;
    uint64_t generation = 0;
    nativeui::osc::Bank native;
    nativeui::NativeCellBank<nativeui::osc::kSettingsAll> settings;
    bool useNativeSettings = false;
    nativeui::oscdetail::Bank detail;
    bool useNativeDetail = false;
    int warpMask = 0;
    std::string sourceLabel;
    void setNativeOscAbcState (int mask, const char* label) override { warpMask = mask; sourceLabel = label; }

private:
    float v_[synthyboy::kMaxParams] = {};
};

TouchEvent ev (TouchEvent::Kind k, int x, int y) { TouchEvent e; e.kind = k; e.x = x; e.y = y; return e; }

struct Rig
{
    OscPageUi page;
    Inst inst;
    UiStack stack;
    double now = 1000.0;
    int sel;
    const float* targets = nullptr;
    explicit Rig (int selector) : sel (selector)
    {
        const char* const power[] = { "sub.on", "osc1.on", "osc2.on", "osc3.on", "noise.on" };
        inst.set (power[std::clamp (selector, 1, 5) - 1], 1.0f);
        inst.sel = 3;   // the Play page's selector says oscillator B: a page of another oscillator must not follow it
        stack.openFull (OscPageUi::kElementPage);   // what the controller does for a page view
        refresh();
    }
    void refresh()
    {
        now += 50.0;
        page::Input in;
        in.instance = sel;
        in.targets = targets;
        page.update (inst, stack, now, in);
    }
    void tap (int cx, int cy)
    {
        page.touch (ev (TouchEvent::Down, cx, cy), stack, inst, now);
        now += 40;
        page.tick (now, stack, inst);
        page.touch (ev (TouchEvent::Up, cx, cy), stack, inst, now);
        now += 100.0;
        refresh();
    }
    void drag (int x0, int y0, int x1, int y1)
    {
        page.touch (ev (TouchEvent::Down, x0, y0), stack, inst, now);
        for (int i = 1; i <= 10; ++i)
        {
            now += 16.0;
            page.touch (ev (TouchEvent::Move, x0 + (x1 - x0) * i / 10, y0 + (y1 - y0) * i / 10), stack, inst, now);
        }
        page.touch (ev (TouchEvent::Up, x1, y1), stack, inst, now);
        now += 100.0;
        refresh();
    }
};

void printNativeSettingsContract()
{
    namespace n = nativeui::osc;
    using Type = OscUi::T;
    struct Variant { int source, mode, warp; };
    static constexpr Variant variants[] {
        {1, 0, 0}, {5, 0, 0}, {5, 1, 0},
        {2, 0, 0}, {2, 0, 1}, {2, 0, 2}, {2, 0, 3},
        {2, 1, 0}, {2, 1, 1}, {2, 2, 0}, {2, 3, 0}, {2, 3, 1}, {2, 4, 0}, {2, 4, 1}
    };
    std::cout << "[\n";
    bool firstVariant = true;
    for (const auto variant : variants)
    {
        if (! firstVariant)
            std::cout << ",\n";
        firstVariant = false;
        Rig r (variant.source);
        r.page.setHostBackground (false);
        if (variant.source == playosc::kNoise)
            r.inst.set ("noise.type", static_cast<float> (variant.mode));
        if (variant.source == 2)
        {
            r.inst.set ("osc1.type", static_cast<float> (variant.mode));
            r.inst.set ("osc1.warp.type", (variant.warp & 1) != 0 ? 1.0f : 0.0f);
            r.inst.set ("osc1.warp2.type", (variant.warp & 2) != 0 ? 1.0f : 0.0f);
        }
        r.refresh();
        auto& osc = r.page.ui();
        std::cout << "  {\"layout\": " << osc.nativeSettingsView();
        if (variant.source == 2)
            std::cout << ", \"warp\": " << osc.nativeWarpMask();
        std::cout << ", \"fields\": [";
        const auto fields = osc.nativeSettingsItems();
        bool first = true;
        for (size_t slot = 0; slot < fields.size(); ++slot)
        {
            const auto* item = fields[slot];
            if (item == nullptr)
                continue;
            const auto binding = OscUi::nativeSettingsBinding (*item);
            const auto* pi = item->param >= 0 ? &synthyboy::paramTable()[item->param] : nullptr;
            const auto rect = item->c->rect();
            const char* kind = item->type == Type::Knob ? (pi->min < 0.0f && pi->max > 0.0f ? "dial_bi" : "dial_uni")
                             : binding.kind == n::SettingsToggle ? "toggle"
                             : binding.kind == n::SettingsNumber || binding.kind == n::SettingsOctave || binding.kind == n::SettingsVoices ? "number"
                             : binding.kind == n::SettingsType || binding.kind == n::SettingsMenu ? "menu"
                             : binding.kind == n::SettingsSource ? (item->type == Type::Table ? "table" : "source") : "action";
            double min = pi != nullptr ? pi->min : 0.0, max = pi != nullptr ? pi->max : 1.0, step = pi != nullptr ? pi->interval : 1.0;
            if (item->type == Type::Number || item->type == Type::NumberOct)
            {
                const auto& state = static_cast<const ui::NumberField*> (item->c)->state;
                min = state.min;
                max = state.max;
                step = state.step;
            }
            if (! first)
                std::cout << ',';
            first = false;
            std::cout << "\n    {\"slot\": " << (variant.source == 2 ? slot - n::kSettings : slot)
                      << ", \"id\": " << item->id << ", \"key\": " << std::quoted (item->key)
                      << ", \"label\": " << std::quoted (OscUi::nativeSettingsLabel (*item)) << ", \"kind\": " << std::quoted (kind)
                      << ", \"rect\": [" << rect.x << ", " << rect.y - page::kTabY << ", " << rect.w << ", " << rect.h << "]"
                      << ", \"min\": " << min << ", \"max\": " << max << ", \"step\": " << step << ", \"choices\": [";
            if (pi != nullptr && ! n::settingsAction (binding.kind))
                for (unsigned i = 0; i < pi->numChoices; ++i)
                {
                    const int choice = binding.kind == n::SettingsType ? n::kTypeChoices[i] : static_cast<int> (i);
                    std::cout << (i == 0 ? "" : ", ") << std::quoted (ui::shownChoice (pi->key, choice, pi->choices[choice]));
                }
            std::cout << "]}";
        }
        std::cout << "\n  ]}";
    }
    std::cout << "\n]\n";
}

void testNativeSettings()
{
    namespace n = nativeui::osc;
    struct Case { int source, mode, layout, fields; const char* power; };
    const Case cases[] { {1, 0, n::SettingsSub, 7, "sub.on"},
                        {5, 0, n::SettingsNoise, 8, "noise.on"},
                        {5, 1, n::SettingsNoiseSample, 11, "noise.on"} };
    for (const auto& c : cases)
    {
        Rig r (c.source);
        r.inst.useNativeSettings = true;
        r.page.setHostBackground (false);
        r.inst.set ("noise.type", static_cast<float> (c.mode));
        r.refresh();
        const auto fields = r.page.ui().nativeSettingsItems();
        auto scene = std::make_unique<gpu::SceneData>();
        gpu::SceneBuilder sb (*scene);
        sb.begin (gpu::kKindPlay, 0, 1280, 800);
        r.page.draw (sb);
        std::vector<uint32_t> pixels;
        renderScene (*scene, pixels);
        int seen = 0;
        bool matched = r.inst.settings.layout() == c.layout && ! sb.overflow;
        for (int slot = 0; slot < n::kSettings; ++slot)
        {
            const auto* item = fields[slot];
            if (item == nullptr)
            {
                matched &= r.inst.settings.at (slot).cell().target < 0;
                continue;
            }
            ++seen;
            const auto rect = item->c->rect();
            const int x = rect.x + rect.w / 2, y = rect.y + rect.h / 2;
            if (item->type == OscUi::T::Knob)
            {
                bool transparent = true;
                for (int py = rect.y + 4; py < rect.bottom() - 4; ++py)
                    for (int px = rect.x + 4; px < rect.right() - 4; ++px)
                        transparent &= pixels[py * 1280 + px] == 0;
                check (transparent, "native knob has no duplicate overlay dial, title or value: " + item->key);
            }
            matched &= r.inst.settings.at (slot).cell().target == item->param && pixels[y * 1280 + x] == 0
                && ! r.page.covers (x, y) && ! r.page.touch (ev (TouchEvent::Down, x, y), r.stack, r.inst, r.now)
                && ! r.page.wheel (x, y, 1, r.stack, r.inst, r.now);
            if (item->key == "noise.type")
                continue;
            const auto& pi = synthyboy::paramTable()[item->param];
            const float current = r.inst.param (item->param);
            const float requested = synthyboy::toNormalised (pi, current) > 0.5f ? 0.0f : 1.0f;
            const int writes = r.inst.writes;
            const int target = item->param;
            r.inst.postSetting (slot, requested);
            r.refresh();
            matched &= r.inst.writes == writes + 1 && r.inst.lastWritten == target
                && std::fabs (r.inst.param (target) - (requested > 0.5f ? pi.max : pi.min)) < 0.001;
            if (c.source == 1 && item->type == OscUi::T::Toggle)
            {
                r.inst.postSetting (slot, synthyboy::toNormalised (pi, current));
                r.refresh();
                matched &= r.inst.writes == writes + 2 && r.inst.lastWritten == target
                    && r.inst.param (target) == current;
            }
        }
        check (seen == c.fields && matched, "all " + std::to_string (c.fields) + " native UI fields own their rendered cells and exact targets in layout "
               + std::to_string (c.layout));
        const auto& cell = fields[c.source == 1 ? 2 : 9]->c->rect();
        const int writes = r.inst.writes;
        r.inst.postSetting (c.source == 1 ? 2 : 9, 0.53f);
        r.inst.set (c.power, 0.0f);
        r.refresh();
        check (r.inst.settings.layout() == c.layout && ! r.page.covers (cell.x + 2, cell.y + 4) && r.inst.writes == writes,
               "OFF keeps native settings visible and rejects their queued edit");
    }

    Rig noise (5);
    noise.inst.useNativeSettings = true;
    noise.page.setHostBackground (true);
    noise.inst.set ("noise.type", 0.0f);
    noise.refresh();
    const int before = noise.inst.writes;
    noise.inst.postSetting (n::kSubSettings, 1.0f);
    noise.inst.postSetting (8, 1.0f);   // SOUND in the old generated layout
    noise.inst.postSetting (9, 0.15f);  // FILTER moves between layouts
    noise.refresh();
    check (noise.inst.settings.layout() == n::SettingsNoiseSample && noise.inst.get ("noise.type") == 1.0f
           && noise.inst.writes == before + 1 && noise.inst.settings.at (8).cell().target < 0,
           "noise KIND switches to sample and expires both hidden and moved controls from the previous layout");
    noise.inst.postSetting (11, 0.63f);
    noise.refresh();
    check (noise.inst.get ("noise.pitch") == 12.0f && noise.inst.lastWritten == Inst::idx ("noise.pitch"),
           "the Noise PITCH keypad value follows the existing signed integer range");
    const int edits = noise.inst.writes;
    noise.inst.postSetting (15, 1.0f);
    ++noise.inst.generation;
    noise.refresh();
    check (noise.inst.writes == edits && noise.inst.settings.layout() == n::SettingsNoiseSample,
           "preset recall expires a setting even when the same Noise layout is restored");
    noise.inst.postSetting (15, 1.0f);
    noise.stack.openDetail (OscUi::kPageRoute);
    noise.refresh();
    check (noise.inst.settings.layout() == n::SettingsHidden && noise.inst.writes == edits,
           "an overlay detail closes the native settings bank before consuming a queued edit");
    noise.stack.back();
    noise.refresh();
    check (noise.inst.settings.layout() == n::SettingsNoiseSample && noise.inst.writes == edits,
           "closing the detail restores the current settings without replaying older writes");
}

void testNativeAbcSettings()
{
    namespace n = nativeui::osc;
    for (int source = 2; source <= 4; ++source)
    {
        const auto prefix = "osc" + std::to_string (source - 1) + ".";
        for (int raw = 0; raw < 5; ++raw)
        {
            Rig r (source);
            r.inst.useNativeSettings = true;
            r.page.setHostBackground (raw % 2 != 0);
            r.inst.set (prefix + "type", static_cast<float> (raw));
            r.inst.set (prefix + "warp.type", 1.0f);
            r.inst.set (prefix + "warp2.type", 1.0f);
            r.inst.set (prefix + "transpose", -15.0f);
            r.refresh();
            const auto fields = r.page.ui().nativeSettingsItems();
            bool matched = r.inst.settings.layout() == n::SettingsWavetable + raw && ! r.inst.sourceLabel.empty();
            auto scene = std::make_unique<gpu::SceneData>();
            gpu::SceneBuilder sb (*scene);
            sb.begin (gpu::kKindPlay, 0, 1280, 800);
            r.page.draw (sb);
            std::vector<uint32_t> pixels;
            renderScene (*scene, pixels);
            matched &= ! sb.overflow;
            int count = 0;
            for (int slot = 0; slot < n::kSettingsAll; ++slot)
            {
                auto* item = fields[slot];
                if (item == nullptr)
                {
                    matched &= r.inst.settings.at (slot).cell().target < 0;
                    continue;
                }
                ++count;
                const auto cell = r.inst.settings.at (slot).cell();
                const auto rect = item->id == OscUi::kTuneOct && r.page.isNativeTuneActive()
                    ? OscPageUi::tuneArea() : item->c->rect();
                const int x = rect.x + rect.w / 2, y = rect.y + rect.h / 2;
                matched &= (item->key.empty() || item->key.find (prefix) == 0)
                    && slot >= n::abcSettingIndex (source, 0) && slot < n::abcSettingIndex (source, 0) + n::kAbcSettings
                    && ! r.page.covers (x, y) && pixels[y * 1280 + x] == 0;
                if (n::settingsAction (cell.kind) || cell.kind == n::SettingsType || cell.kind == n::SettingsOctave
                    || cell.kind == n::SettingsMenu)
                    continue;
                const auto& pi = synthyboy::paramTable()[item->param];
                const float current = r.inst.param (item->param);
                const float requested = synthyboy::toNormalised (pi, current) > 0.5f ? 0.0f : 1.0f;
                const int writes = r.inst.writes;
                r.inst.postSetting (slot, requested);
                r.refresh();
                matched &= r.inst.writes == writes + 1 && r.inst.lastWritten == item->param
                    && std::fabs (r.inst.param (item->param) - (requested > 0.5f ? pi.max : pi.min)) < 0.001;
            }
            check (count >= 10 && matched, prefix + "type " + std::to_string (raw) + " renders and edits only its declared native fields");
            const int before = r.inst.writes;
            const int transpose = Inst::idx (prefix + "transpose");
            r.inst.postSetting (n::abcSettingIndex (source, n::AbcOctave), 0.75f);
            r.refresh();
            check (r.inst.param (transpose) == 21.0f && r.inst.writes == before + 1 && r.inst.lastWritten == transpose,
                   prefix + "OCT preserves the signed semitone remainder");
        }
    }

    Rig r (2);
    r.inst.useNativeSettings = true;
    r.page.setHostBackground (false);
    r.inst.set ("osc1.type", 0);
    r.inst.set ("osc2.on", 1);
    r.inst.set ("osc1.warp.type", 1);
    r.inst.set ("osc1.warp2.type", 0);
    r.refresh();
    const auto slot = [] (int field) { return n::abcSettingIndex (2, field); };
    const auto& oldWarpRect = r.page.ui().full().panel.warp()->first.type.rect();
    const int activeWidth = oldWarpRect.w;
    const int before = r.inst.writes;
    r.inst.postSetting (slot (n::AbcWarp1Type), 0.0f);
    r.inst.postSetting (slot (n::AbcWarp1Amount), 0.73f);
    r.refresh();
    check (r.inst.warpMask == 0 && r.inst.settings.at (slot (n::AbcWarp1Amount)).cell().target < 0
           && r.page.ui().full().panel.warp()->first.type.rect().w > activeWidth && r.inst.writes == before + 1,
           "switching Warp OFF republishes its wider selector and expires its amount edit");
    r.inst.postSetting (slot (n::AbcWarp1Amount), 1.0f);
    r.refresh();
    check (r.inst.writes == before + 1, "inactive Warp Amount writes nothing");

    r.inst.set ("osc1.wavetable", 1);
    r.refresh();
    const auto tableBefore = r.inst.get ("osc1.wavetable");
    r.inst.postSetting (slot (n::AbcSource), 1.0f);   // next table
    r.refresh();
    check (r.inst.get ("osc1.wavetable") > tableBefore && r.inst.lastWritten == Inst::idx ("osc1.wavetable"),
           "the native SOURCE arrows use the unchanged wavetable stepping");
    const int navigationWrites = r.inst.writes;
    r.inst.postSetting (slot (n::AbcSource), 2.0f / 3.0f);
    r.refresh();
    check (r.page.topPage() == OscUi::kPageList && r.inst.settings.layout() == n::SettingsHidden && r.inst.writes == navigationWrites,
           "the native SOURCE centre opens the existing wavetable browser without writing a parameter");
    r.stack.back();
    r.refresh();

    r.inst.postSetting (slot (n::AbcUnisonDetail), 1.0f);
    r.refresh();
    check (r.page.topPage() == OscUi::kPageUnison && r.inst.settings.layout() == n::SettingsHidden,
           "native UNISON DETAIL opens its own overlay page and closes the root handles");
    r.stack.back();
    r.refresh();
    r.inst.postSetting (slot (n::AbcType), 0.25f);   // second TYPE choice is MULTI, raw value 4
    r.refresh();
    check (r.inst.get ("osc1.type") == 4.0f && r.inst.settings.layout() == n::SettingsMultisample,
           "the native TYPE menu keeps the pictograms’ order and selects the matching raw engine type");
    const int beforeBrowser = r.inst.writes;
    r.inst.roots.push_back ({"Test content", "/tmp"});
    r.inst.postSetting (slot (n::AbcBrowse), 1.0f);
    r.refresh();
    check (r.page.topPage() == OscUi::kPageList && r.inst.writes == beforeBrowser,
           "native SOURCE browse reaches the multisample content picker without a parameter write");
    r.stack.back();
    r.refresh();
    r.inst.native.post (n::kSource, 0.5f);
    r.refresh();
    const int afterSource = r.inst.writes;
    r.inst.postSetting (slot (n::AbcType), 1.0f);
    r.refresh();
    check (r.page.selection() == 3 && r.inst.get ("osc1.type") == 4.0f && r.inst.writes == afterSource,
           "a late Osc A TYPE callback cannot write to Osc B’s current field");
    r.inst.set ("osc2.on", 0);
    r.refresh();
    check (r.inst.settings.layout() != n::SettingsHidden,
           "the switched-off ABC root retains settings and source navigation");
}

void testSourceOwnedRouting()
{
    namespace n = nativeui::osc;
    Rig r (2);
    r.page.setHostBackground (false);
    r.inst.set ("osc2.on", 1.0f);
    r.inst.set ("osc1.destination", 0.0f);
    r.inst.set ("osc2.destination", 0.0f);
    r.refresh();
    r.inst.native.post (n::kRouting, 1.0f, 2);
    r.inst.native.post (n::kSource, 0.5f);
    r.refresh();
    const int before = r.inst.writes;
    check (r.page.selection() == 3 && before == 0, "a queued routing popup choice expires when the source changes");
    r.inst.native.post (n::kRouting, 1.0f, 2);
    r.refresh();
    check (r.inst.writes == before && r.inst.get ("osc1.destination") == 0.0f && r.inst.get ("osc2.destination") == 0.0f,
           "a late routed write to the previous source handle cannot retarget the selected oscillator");
    r.inst.native.post (n::kRouting, 1.0f, 3);
    r.refresh();
    check (r.inst.get ("osc2.destination") == n::kLastRoute && r.inst.lastWritten == Inst::idx ("osc2.destination")
           && r.inst.writes == before + 1, "a fresh source-owned routing write reaches the selected oscillator only");

    Rig b (3);
    b.page.setHostBackground (false);
    b.inst.set ("osc2.destination", 0.0f);
    b.refresh();
    b.inst.native.post (n::kRouting, 1.0f, 3);
    ++b.inst.generation;
    b.refresh();
    check (b.inst.writes == 0 && b.inst.get ("osc2.destination") == 0.0f,
           "preset replacement expires a queued route on the very same source");
    b.inst.native.post (n::kRouting, 1.0f, 3);
    b.page.leftTab (b.inst);
    b.inst.native.post (n::kRouting, 1.0f, 3);
    b.refresh();
    check (b.inst.writes == 0 && r.inst.get ("osc2.destination") == n::kLastRoute,
           "the abandoned view and another VST instance of the same source cannot exchange queued routes");
}

bool hasLeaf (ui::Composite& root, const ui::Component* c)
{
    ui::Component* leaves[256];
    const int n = root.collectLeaves (leaves, 0, 256);
    for (int i = 0; i < n; ++i)
        if (leaves[i] == c)
            return true;
    return false;
}

bool leavesNoOverlap (ui::Component& root, std::string& why)
{
    ui::Component* l[256];
    const int n = root.collectLeaves (l, 0, 256);
    for (int i = 0; i < n; ++i)
    {
        if (l[i]->rect().empty())
            continue;
        for (int j = i + 1; j < n; ++j)
        {
            if (l[j]->rect().empty())
                continue;
            if (l[i]->rect().inset (1).overlaps (l[j]->rect().inset (1)))
            {
                why = "leaf " + std::to_string (i) + " (" + std::to_string (l[i]->rect().x) + "," + std::to_string (l[i]->rect().y) + " " + std::to_string (l[i]->rect().w) + "x" + std::to_string (l[i]->rect().h) + ") overlaps leaf " + std::to_string (j) + " (" + std::to_string (l[j]->rect().x) + "," + std::to_string (l[j]->rect().y) + " " + std::to_string (l[j]->rect().w) + "x" + std::to_string (l[j]->rect().h) + ")";
                return false;
            }
        }
    }
    return n > 0;
}

/** Check that a block has at least one shown, enabled, non-inactive component. */
bool hasUsableControl (const ui::KnobPanel& panel, int blockIdx)
{
    const ui::PanelPreset* p = panel.preset();
    if (! p)
        return false;
    for (int i = 0; i < p->numItems; ++i)
    {
        const ui::PanelItem& it = p->items[i];
        if (it.block != blockIdx)
            continue;
        ui::Component* c = panel.part (it.slot);
        if (c != nullptr && c->enabled() && ! c->inactive() && panel.shown (it.slot))
            return true;
    }
    return false;
}
} // namespace

static void testNoiseSamplePlayhead()
{
    synthyboy::MemoryContentStore store;
    const std::vector<float> samples (1024, 0.25f);
    store.add ("test", "noise.wav", synthyboy::encodeWav (samples.data(), nullptr, samples.size(), 44100, 16));
    auto content = std::make_shared<synthyboy::StateContent>();
    synthyboy::ContentFileRef ref;
    ref.pack = "test";
    ref.file = "noise.wav";
    synthyboy::attachSample (*content, 4, ref, 44100, 1024, 1);
    Inst inst;
    inst.contentPtr = content;
    inst.store = &store;
    inst.set ("noise.on", 1.0f);
    inst.set ("noise.type", 1.0f);
    OscDisplay display (0.0);
    ViewCounters counters;
    synthyboy::LiveValues live;
    auto update = [&](double ms) { display.update (inst, playosc::kNoise, nullptr, &live, ms, counters); };
    auto livePicture = [&] {
        auto scene = std::make_unique<gpu::SceneData>();
        gpu::SceneBuilder sb (*scene);
        sb.begin (gpu::kKindPlay, 0, 1280, 800);
        display.draw (sb, { 10, 120, 500, 220 }, true);
        return page::sceneHash (*scene);
    };
    update (0);
    live.noisePos = 0.25f;
    update (500);
    const auto first = livePicture();
    live.noisePos = 0.75f;
    update (520);
    check (display.kind() == synthyboy::OscCycle::Kind::sample && livePicture() != first,
           "the Noise waveform cursor follows the sample playback position");
    inst.set ("noise.on", 0.0f);
    update (600);
    const auto stopped = livePicture();
    live.noisePos = 0.12f;
    update (620);
    check (display.kind() == synthyboy::OscCycle::Kind::off && livePicture() == stopped,
           "the disabled Noise sample waveform has no moving cursor");
}

static void testNativeSourceSwitch()
{
    namespace n = nativeui::osc;
    Rig r (2);
    float live[12] = { 11.0f, 0.0f, 0.0f, 0.0f, 22.0f, 0.0f, 0.0f, 0.0f, 33.0f };
    r.targets = live;
    r.inst.set ("sub.on", 0.0f);
    r.inst.set ("noise.on", 0.0f);
    r.inst.set ("osc2.on", 1.0f);
    r.inst.set ("osc3.on", 1.0f);
    r.inst.set ("osc1.destination", 1.0f);
    r.inst.set ("osc3.destination", 5.0f);
    r.page.setHostBackground (true);
    r.now += 1000.0;
    r.refresh();
    auto post = [&] (int field, float value)
    {
        r.inst.native.post (field, value);
        r.refresh();
    };
    check (r.inst.native.snapshot().source() == 2 && r.page.display().framePosition() == 11.0f,
           "entering Osc A publishes A and uses the first oscillator readout slice");
    r.inst.native.post (n::kRouting, 0.5f);
    post (n::kSource, 0.75f);
    check (r.inst.writes == 0 && r.inst.get ("osc1.destination") == 1.0f && r.inst.get ("osc3.destination") == 5.0f,
           "a route queued for A is discarded when the same native update selects C");
    check (r.page.selection() == 4 && r.inst.native.snapshot().source() == 4 && r.page.ui().selection() == 4
           && std::string (r.page.ui().full().pageTitle()) == "OSC C" && r.inst.native.snapshot().destination() == 5
           && r.page.display().selection() == 4 && r.page.display().framePosition() == 33.0f
           && r.inst.q[2] == Inst::idx ("osc3.frame") && r.inst.sel == 3 && r.inst.selWrites == 0,
           "C owns the name, routing value, live waveform and Q-Link targets while Play stays on B");
    post (n::kRouting, 0.5f);
    check (r.inst.lastWritten == Inst::idx ("osc3.destination") && r.inst.get ("osc3.destination") == 3.0f
           && r.inst.get ("osc1.destination") == 1.0f && r.inst.writes == 1, "the routing menu writes only C's destination");
    post (n::kSource, 0.5f);
    check (r.page.display().framePosition() == 22.0f && r.inst.q[2] == Inst::idx ("osc2.frame"), "B takes the middle live slice and Q targets");
    for (int src : { 1, 5 })
    {
        post (n::kSource, static_cast<float> (src - 1) / (n::kLastSource - 1));
        check (r.inst.native.snapshot().source() == src && r.inst.native.snapshot().visibility() == n::kRootOff
               && r.page.display().selection() == src && r.page.display().kind() == synthyboy::OscCycle::Kind::off
               && r.page.covers (r.page.routingArea().x + 8, r.page.routingArea().y + 8),
               "OFF source " + std::to_string (src) + " uses its own graph and lets the menu navigate");
        post (n::kRouting, 1.0f);
        check (r.inst.writes == 1, "OFF routing is rejected at the host boundary");
    }
    post (n::kSource, 0.75f);
    r.inst.native.post (n::kRouting, 1.0f);
    r.page.leftTab (r.inst);
    check (r.inst.native.snapshot().visibility() == n::kHidden, "leaving the tab disarms the OSC host controls");
    r.refresh();
    check (r.page.selection() == 2 && r.inst.writes == 1, "returning to Osc A resets to its label and drops old routing");
    post (n::kSource, 0.75f);
    r.inst.native.post (n::kSource, 1.0f);
    ++r.inst.generation;
    r.refresh();
    check (r.page.selection() == 2 && r.inst.writes == 1 && r.inst.selWrites == 0,
           "preset recall resets the tab and discards the previous source request without writing Play or sound");
}

static void testNativeDetail()
{
    namespace n = nativeui::oscdetail;
    const auto named = [] (const Rig& rig, std::string_view name)
    {
        for (int i = 0; i < n::kCells; ++i)
            if (const char* shown = rig.inst.detail.name (i); shown != nullptr && name == shown)
                return i;
        return -1;
    };
    const auto target = [] (const Rig& rig, const char* key)
    {
        const int index = Inst::idx (key);
        for (int i = 0; i < n::kCells; ++i)
            if (index >= 0 && rig.inst.detail.at (i).cell().target == index)
                return i;
        return -1;
    };
    const auto hit = [] (Rig& rig, int field)
    {
        if (field >= 0)
        {
            rig.inst.postDetail (field);
            rig.refresh();
        }
    };
    const auto completeGrid = [] (const Rig& r)
    {
        const auto& osc = r.page.ui();
        const int rows = r.inst.detail.layout() >= n::Graph ? n::kRows - 1 : n::kRows;
        if (r.inst.detail.layout() >= n::Graph)
        {
            const int kind = r.inst.detail.at (n::kRows).cell().kind;
            const int stack = n::kFirstMenuKind + static_cast<int> (std::find (std::begin (n::kMenuSuffixes), std::end (n::kMenuSuffixes), "unison.stack") - std::begin (n::kMenuSuffixes));
            if (kind > stack || (kind == stack) != (r.page.topPage() == OscUi::kPageUnison))
                return false;
        }
        const ui::Rect tab {0, OscUi::kTabY, 1280, 628};
        for (const auto& item : osc.pageItems())
        {
            const bool spec = oscspec::isSpecPage (r.page.topPage());
            if (item.alwaysOff || item.type == OscUi::T::Mask || (item.param < 0 && item.type != OscUi::T::Inert)
                || (spec && ! osc.specPage().visible (item.id - OscUi::kSpec)))
                continue;
            int matches = 0;
            for (int i = 0; i < n::kCells; ++i)
            {
                const auto binding = r.inst.detail.at (i).cell();
                if (binding.target >= 0 && binding.source == item.id)
                {
                    if (i % n::kRows >= rows || ! tab.contains (osc.nativeDetailRect (i)))
                        return false;
                    ++matches;
                }
            }
            if (matches != 1)
                return false;
        }
        return true;
    };
    for (int source = 1; source <= 5; ++source)
    {
        Rig r (source);
        r.inst.useNativeDetail = true;
        r.refresh();
        check (r.inst.detail.layout() == (source == 1 || source == 5 ? n::RootSmall : n::Root),
               "every standalone source publishes MORE at its actual root rectangle");
        if (source == 1 || source == 5)
        {
            const char* blendKey = source == 1 ? "sub.filter_blend" : "noise.filter_blend";
            check (r.inst.detail.at (0).cell().target == Inst::idx (blendKey), "root BLEND targets its real source parameter");
            check (r.inst.detail.at (n::kMore).cell().target < 0, "Sub/Noise has no MORE binding");
            r.inst.postDetail (0, 0.73f);
            r.refresh();
            check (std::abs (r.inst.get (blendKey) - 73.0f) < 0.01f, "root native BLEND writes the true parameter");
            r.stack.openDetail (oscspec::kMore);
            r.refresh();
            check (r.page.topPage() == 0 && r.stack.depth() == 1, "legacy Sub/Noise MORE restores its own root");
            const int writes = r.inst.writes;
            hit (r, n::kMore);
            check (r.page.topPage() == 0 && r.inst.writes == writes, "obsolete MORE callback is inert");
            r.inst.postDetail (0, 0.2f);
            ++r.inst.generation;
            r.refresh();
            check (r.inst.writes == writes, "preset replacement rejects stale root BLEND");
            r.inst.set (source == 1 ? "sub.on" : "noise.on", 0.0f);
            r.refresh();
            r.inst.postDetail (0, 0.9f);
            r.refresh();
            check (r.inst.writes == writes && r.inst.detail.layout() == n::Hidden, "OFF root BLEND is read-only");
            continue;
        }
        hit (r, n::kMore);
        check (r.page.topPage() == oscspec::kMore && r.inst.detail.layout() == n::Plain && r.inst.writes == 0,
               "native MORE opens independently of the type, without a parameter write");
        check (completeGrid (r), "every control on the source's MORE sheet has one in-bounds cell");
        hit (r, n::kBack);
        check (r.page.topPage() == 0 && r.stack.depth() == 1, "native BACK returns to the source root");
    }
    // The fallback keeps the same physical target and OFF protection without a native host.
    for (int source : {1, 5})
    {
        Rig r (source);
        const char* key = source == 1 ? "sub.filter_blend" : "noise.filter_blend";
        const auto& blend = source == 1 ? static_cast<const ui::Component&> (r.page.ui().full().subBlend)
            : *r.page.ui().full().panel.part (ui::PanelSlot::SubBlend);
        const auto area = blend.rect();
        const int x = area.x + area.w / 2, y = area.y + area.h / 2;
        r.drag (x, y, x, y - 50);
        check (r.inst.lastWritten == Inst::idx (key) && r.inst.get (key) > 0.0f
               && r.inst.q[3] == Inst::idx (key), "fallback BLEND drag and physical Q4 own the same target");
        const int writes = r.inst.writes;
        r.inst.set (source == 1 ? "sub.on" : "noise.on", 0.0f);
        r.refresh();
        r.drag (x, y, x, y - 50);
        check ((source == 1 ? !blend.enabled() : !r.page.ui().full().panel.enabled()) && blend.inactive() && r.inst.writes == writes && r.inst.q[3] < 0, std::string ("OFF fallback BLEND is grey and cannot write: ") + key + " enabled=" + std::to_string (blend.enabled()) + " inactive=" + std::to_string (blend.inactive()) + " writes=" + std::to_string (r.inst.writes) + "/" + std::to_string (writes));
    }
    for (int mode : {0, 1})
    {
        Rig r (5);
        r.inst.set ("noise.type", static_cast<float> (mode));
        r.inst.useNativeDetail = r.inst.useNativeSettings = true;
        r.refresh();
        const auto area = r.page.ui().nativeDetailRect (0);
        const int x = area.x + area.w / 2, y = area.y + area.h / 2;
        check (area.w >= 56 && area.h >= 56 && area == r.page.ui().full().panel.part (ui::PanelSlot::SubBlend)->rect(),
               "both Noise modes share the live BLEND input and visual rectangle");
        check (r.page.ui().nativeDetailFieldAt (x, y) == 0
               && !r.page.touch (ev (TouchEvent::Down, x, y), r.stack, r.inst, r.now),
               "Noise BLEND passes native touch through its actual aperture");
        r.page.touch (ev (TouchEvent::Up, x, y), r.stack, r.inst, r.now);
        r.refresh();
        check (r.inst.q[3] == Inst::idx ("noise.filter_blend"), "focusing Noise BLEND maps physical Q4 to its exact target");
        auto scene = std::make_unique<gpu::SceneData>();
        gpu::SceneBuilder sb (*scene);
        sb.begin (gpu::kKindPlay, 0, 1280, 800);
        r.page.draw (sb);
        std::vector<uint32_t> pixels;
        renderScene (*scene, pixels);
        check (pixels[1280 * y + x] == 0, "Noise BLEND overlay aperture is transparent to the native blue knob");
        r.inst.postDetail (0, 0.41f);
        r.refresh();
        check (r.inst.lastWritten == Inst::idx ("noise.filter_blend") && std::fabs (r.inst.get ("noise.filter_blend") - 41.0f) < 0.01f,
               "both Noise root modes edit the source BLEND without aliasing");
    }
    struct Sheet { int type, page; const char* opener; const char* first; };
    const Sheet sheets[] {
        {0, oscspec::kWtMap, "MAP", "osc1.remap.on"},
        {0, oscspec::kOptions, "OPTIONS", "osc1.on"},
        {1, oscspec::kSampleRegion, "REGION", "osc1.sample.start"},
        {1, oscspec::kSampleSlice, "SLICE", "osc1.sample.slice_mode"},
        {2, oscspec::kGrain, "GRAIN", "osc1.gran.density_mode"},
        {2, OscUi::kPageGrain, "WINDOW", "osc1.gran.window_shape"},
        {2, OscUi::kPageUnison, "UNISON", "osc1.unison.stack"},
        {3, oscspec::kSpecFilter, "FILTER", "osc1.spec.freq_lo"},
        {3, oscspec::kSpecScan, "SCAN", "osc1.spec.manual"},
        {4, OscUi::kPageEnvelope, "ENV", "osc1.multi.override"},
    };
    for (const auto& sheet : sheets)
    {
        Rig r (2);
        r.inst.useNativeDetail = true;
        r.inst.set ("osc1.type", static_cast<float> (sheet.type));
        r.inst.set ("osc1.pitch_track", 0.0f);
        r.inst.set ("osc1.remap.on", 1.0f);
        r.inst.set ("osc1.sample.keytrack", 1.0f);
        r.inst.set ("osc1.sample.loop_mode", 1.0f);
        r.inst.set ("osc1.sample.slice_mode", 2.0f);
        r.inst.set ("osc1.sample.scan_to_bpm", 1.0f);
        r.inst.set ("osc1.gran.density_mode", 1.0f);
        r.inst.set ("osc1.gran.length_mode", 1.0f);
        r.inst.set ("osc1.gran.y_axis", 1.0f);
        r.inst.set ("osc1.spec.flt_use", 1.0f);
        r.inst.set ("osc1.spec.y_axis", 1.0f);
        r.inst.set ("osc1.warp.type", 1.0f);
        r.inst.set ("osc1.warp2.type", 1.0f);
        r.refresh();
        hit (r, n::kMore);
        check (completeGrid (r), std::string ("every enabled MORE control fits for ") + sheet.opener);
        hit (r, named (r, sheet.opener));
        const int field = target (r, sheet.first);
        check (r.page.topPage() == sheet.page && field >= 0,
               std::string ("native navigation and real parameter reach ") + sheet.opener);
        check (completeGrid (r), std::string ("all visible controls have one on-screen skin cell for ") + sheet.opener);
        if (field < 0)
            continue;
        const auto rect = r.page.ui().nativeDetailRect (field);
        const int x = rect.x + rect.w / 2, y = rect.y + rect.h / 2;
        auto scene = std::make_unique<gpu::SceneData>();
        gpu::SceneBuilder sb (*scene);
        sb.begin (gpu::kKindOsc, 0, 1280, 800);
        r.page.draw (sb);
        if (sheet.page == oscspec::kWtMap)
        {
            int markers = 0, frames = 0;
            const uint32_t colour = ui::colour::rgba (ui::theme::L().q, 255);
            for (uint32_t i = 0; i < scene->numOps; ++i)
            {
                const auto& op = scene->ops[i];
                markers += op.op == gpu::kOpText && gpu::textString (*scene, op) == "Q";
                frames += op.op == gpu::kOpRRect && op.rgba == colour && op.rgba2 != 0;
            }
#if SYNTHYBOY_MPC_QFRAME_ENABLED
            check (markers == 1 && frames == 1, "Gen1 native OSC detail draws exactly one active group frame with an unnumbered Q");
#else
            check (markers == 0 && frames == 0, "Gen2 native OSC detail emits no Q frame or marker");
#endif
        }
        std::vector<uint32_t> pixels;
        renderScene (*scene, pixels);
        check (! r.page.covers (x, y) && pixels[1280 * y + x] == 0 && pixels[1280 * 173 + 160] != 0,
               std::string ("the real scene paints furniture around the transparent hit target for ") + sheet.opener);
        r.tap (x, y);
        check (r.inst.writes == 0, "native aperture touch produces no competing overlay write");
        if (sheet.page == oscspec::kOptions)
        {
            const auto& mask = *r.page.ui().specPage().control (15);
            const auto area = mask.rect();
            check (area.w >= mask.minSize().w && area.h >= mask.minSize().h, "the bespoke two-row note mask keeps its full touch geometry");
            r.tap (area.x + 20, area.bottom() - 20);
            check (r.inst.writes == 1 && r.inst.lastWritten == Inst::idx ("osc1.transpose_snap")
                   && r.inst.q[0] == Inst::idx ("osc1.transpose_snap"),
                   "the bespoke note mask writes the selected note and activates its original Q group");
            const int shape = target (r, "osc1.unison.blend_shape");
            if (shape >= 0)
                r.inst.postDetail (shape, 0.95f);
            r.inst.postDetail (field, 0.0f);
            r.refresh();
            r.refresh();
            check (shape >= 0 && r.page.topPage() == 0 && r.inst.writes == 2 && r.inst.lastWritten == Inst::idx ("osc1.on"),
                   "same-tick power OFF rejects a competing native edit and returns to the source");
        }
    }
    Rig r (2);
    r.inst.useNativeDetail = true;
    r.inst.set ("osc1.type", 1.0f);
    r.refresh();
    hit (r, n::kMore);
    const int warp = target (r, "osc1.warp2.type");
    if (warp < 0)
    {
        check (false, "sample MORE publishes the full Warp 2 menu");
        return;
    }
    const auto cell = r.page.ui().nativeDetailRect (warp);
    const int x = cell.x + cell.w / 2, y = cell.y + cell.h / 2;
    const int menu = r.page.ui().nativeDetailMenu (warp);
    r.page.touch (ev (TouchEvent::Down, x, y), r.stack, r.inst, r.now);
    r.page.setNativeActionPopup (r.inst, -2);
    r.inst.detail.postMenu (menu, 0.5f, r.inst.generation);
    r.refresh();
    check (r.inst.writes == 1 && r.inst.lastWritten == Inst::idx ("osc1.warp2.type"),
           "the marked Warp 2 drawer writes its actual named choice exactly once");
    r.page.setNativeActionPopup (r.inst, -1);
    r.inst.detail.postMenu (menu, 0.9f, r.inst.generation);
    r.refresh();
    check (r.inst.writes == 1, "the closed drawer rejects its late result");
    r.page.touch (ev (TouchEvent::Down, x, y), r.stack, r.inst, r.now);
    r.page.setNativeActionPopup (r.inst, -2);
    const auto old = r.inst.detail.at (warp);
    ++r.inst.generation;
    r.inst.detail.postMenu (menu, 0.8f, r.inst.generation);
    r.refresh();
    hit (r, n::kMore);
    r.inst.detail.post (old, 0.6f, r.inst.generation);
    r.inst.detail.postMenu (menu, 0.7f, r.inst.generation);
    r.refresh();
    check (r.inst.writes == 1, "a preset reload invalidates both old bindings and a pending drawer across the same page");
    r.inst.set ("osc1.on", 0.0f);
    r.refresh();
    hit (r, n::kMore);
    check (r.inst.detail.layout() == n::Hidden && r.page.topPage() == 0 && r.inst.writes == 1,
           "OFF closes the detail surface and disarms its controls");
}

static void testNativeRootLoad()
{
    namespace n = nativeui::oscdetail;
    for (int source = 2; source <= 4; ++source)
    {
        const int osc = source - 1, field = n::kRootLoad + osc - 1;
        const std::string key = "osc" + std::to_string (osc);
        Rig r (source);
        r.inst.useNativeDetail = true;
        r.inst.roots = { { "LIB", "/tmp" } };
        for (int type = 1; type <= 4; ++type)
        {
            r.inst.set (key + ".type", static_cast<float> (type));
            r.refresh();
            const auto& ui = r.page.ui();
            const auto area = ui.loadButton().rect();
            const int x = area.x + area.w / 2, y = area.y + area.h / 2;
            const char* name = r.inst.detail.name (field);
            check (name != nullptr && std::string_view (name) == (type == 4 ? "LOAD INSTRUMENT" : "LOAD SAMPLE")
                   && ui.nativeDetailRect (field) == area && ui.nativeDetailFieldAt (x, y) == field
                   && ui.nativeDetailFieldAt (area.x - 1, y) == -1 && ui.covers (area.x - 1, y),
                   "empty file types publish their own LOAD caption and keep the adjacent plot gesture");
            auto scene = std::make_unique<gpu::SceneData>();
            gpu::SceneBuilder sb (*scene);
            sb.begin (gpu::kKindPlay, 0, 1280, 800);
            r.page.draw (sb);
            std::vector<uint32_t> pixels;
            renderScene (*scene, pixels);
            const bool taken = r.page.touch (ev (TouchEvent::Down, x, y), r.stack, r.inst, r.now);
            r.tap (x, y);
            check (! taken && pixels[1280 * y + x] == 0 && r.stack.depth() == 1,
                   "root LOAD aperture passes through and never opens the browser from the fallback touch path");
            r.inst.postDetail (field);
            r.refresh();
            check (r.page.topPage() == OscUi::kPageList && r.stack.depth() == 2 && r.inst.writes == 0,
                   "the active source-bound command opens the original content browser");
            r.stack.back();
            r.refresh();

            auto saved = std::make_shared<synthyboy::StateContent>();
            if (type == 4)
            {
                synthyboy::ContentInstrument loaded;
                loaded.osc = osc;
                loaded.ref.file = "loaded.sfz";
                saved->instruments.push_back (loaded);
            }
            else
            {
                synthyboy::ContentSample loaded;
                loaded.osc = osc;
                loaded.ref.file = "loaded.wav";
                saved->samples.push_back (loaded);
            }
            r.inst.postDetail (field);
            r.inst.contentPtr = saved;
            r.refresh();
            check (r.page.topPage() == 0 && r.inst.detail.at (field).cell().target < 0,
                   "already loaded sample or instrument suppresses root LOAD and rejects a queued action");
            r.inst.contentPtr.reset();
            r.inst.loadState = 1;
            r.refresh();
            check (r.inst.detail.at (field).cell().target < 0, "an in-progress file replacement hides root LOAD");
            r.inst.loadState = 0;
        }

        r.refresh();
        r.inst.postDetail (field);
        ++r.inst.generation;
        r.refresh();
        check (r.page.topPage() == 0, "preset recall invalidates a queued LOAD for the same oscillator");
        r.inst.postDetail (field);
        r.inst.set (key + ".on", 0.0f);
        r.refresh();
        check (r.page.topPage() == 0 && r.inst.detail.at (field).cell().target < 0,
               "OFF hides LOAD and rejects its pending navigation");
    }

    Rig r (2);
    r.inst.useNativeDetail = true;
    r.inst.roots = { { "LIB", "/tmp" } };
    r.inst.set ("osc1.type", 1.0f);
    r.inst.set ("osc2.type", 1.0f);
    r.inst.set ("osc2.on", 1.0f);
    r.refresh();
    r.inst.postDetail (n::kRootLoad);
    r.sel = 3;
    r.refresh();
    r.inst.postDetail (n::kRootLoad);
    r.refresh();
    check (r.page.topPage() == 0 && r.inst.detail.at (n::kRootLoad).cell().target < 0
           && r.inst.detail.at (n::kRootLoad + 1).cell().target >= 0,
           "queued and late Osc A LOAD cannot open Osc B's browser");
}

static void printNativeDetailContract()
{
    namespace n = nativeui::oscdetail;
    Rig r (2);
    r.page.setHostBackground (true);
    r.inst.useNativeDetail = true;
    r.refresh();
    auto printRect = [] (const Rig& rig, int field, const char* key)
    {
        const auto cell = rig.page.ui().nativeDetailRect (field);
        std::printf ("  \"%s\": [%d, %d, %d, %d],\n", key, cell.x, cell.y - OscUi::kTabY, cell.w, cell.h);
    };
    std::printf ("{\n");
    printRect (r, n::kMore, "more");
    Rig sub (1);
    sub.page.setHostBackground (true);
    sub.refresh();
    printRect (sub, 0, "sub_blend");
    Rig noise (5);
    noise.page.setHostBackground (true);
    noise.refresh();
    printRect (noise, 0, "noise_blend");
    noise.inst.set ("noise.type", 1.0f);
    noise.refresh();
    printRect (noise, 0, "noise_sample_blend");
    printRect (r, n::kBack, "back");
    printRect (r, n::kLoad, "load");
    r.inst.set ("osc1.type", 1.0f);
    r.refresh();
    printRect (r, n::kRootLoad, "root_load");
    std::printf ("  \"plain\": [%d, %d, %d, %d, %d, %d],\n", n::kLeft, n::kPlainTop, n::kWidth, n::kHeight, n::kPlainStep, n::kRows);
    std::printf ("  \"graph\": [%d, %d, %d, %d, %d, %d],\n", n::kLeft, n::kGraphTop, n::kWidth, n::kHeight, n::kGraphStep, n::kRows - 1);
    std::printf ("  \"menus\": [");
    for (int i = 0; i < n::kMenus; ++i)
        std::printf ("%s\"%.*s\"", i == 0 ? "" : ", ", static_cast<int> (n::kMenuSuffixes[i].size()), n::kMenuSuffixes[i].data());
    std::printf ("]\n}\n");
}

static void testHostPaintedKnobLive()
{
    ui::Knob knob { "BLEND" };
    knob.place ({20, 20, 140, 140});
    knob.state.value = 0.4;
    knob.state.modValue = 0.7;
    knob.setHostPainted (true);
    auto scene = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder sb (*scene);
    sb.begin (gpu::kKindPlay, 0, 1280, 800);
    const auto empty = scene->numOps;
    knob.draw (sb);
    check (scene->numOps == empty, "native host owns knob base including its title and value");
    knob.drawLive (sb);
    check (scene->numOps > empty && !sb.overflow, "explicit overlay live animation remains available on a host-painted knob");
}

int main (int argc, char** argv)
{
    if (argc == 2 && std::string_view (argv[1]) == "--native-settings-contract")
    {
        printNativeSettingsContract();
        return 0;
    }
    if (argc == 2 && std::string_view (argv[1]) == "--native-detail-contract")
    {
        printNativeDetailContract();
        return 0;
    }
    testHostPaintedKnobLive();
    testNativeSettings();
    testNativeAbcSettings();
    testNativeDetail();
    testNativeRootLoad();
    testSourceOwnedRouting();
    testNativeSourceSwitch();
    testNoiseSamplePlayhead();
    {
        // the pages of the oscillator types on the OSC tab: BACK from each one returns to the page, the display draws a loaded sample and a non-default spectral setting
        for (int pageId = 16; pageId <= 22; ++pageId)
        {
            Rig r (2);
            r.inst.set ("osc1.type", pageId == 16 ? 0.0f : (pageId == 18 || pageId == 19 ? 1.0f : (pageId == 20 ? 2.0f : 3.0f)));
            r.refresh();
            r.stack.openDetail (pageId);
            r.refresh();
            check (r.page.topPage() == pageId, "page " + std::to_string (pageId) + ": it opens over the page of the oscillator");
            const ui::Rect back = r.page.ui().specPage().back.rect();
            r.tap (back.x + back.w / 2, back.y + back.h / 2);
            check (r.page.topPage() == 0 && r.stack.depth() == 1, "page " + std::to_string (pageId) + ": BACK returns to the page");
        }
        Rig r (2);
        r.inst.set ("osc1.type", 1.0f);
        auto content = std::make_shared<synthyboy::StateContent>();
        synthyboy::ContentSample cs;
        cs.osc = 1; cs.ref.pack = "p"; cs.ref.file = "a.wav"; cs.ref.name = "a";
        content->samples.push_back (cs);
        r.inst.contentPtr = content;
        r.refresh();
        const OscMarks m0 = OscMarks::of (r.inst, 2, 0);
        r.inst.set ("osc1.sample.start", 25.0f);
        const OscMarks m1 = OscMarks::of (r.inst, 2, 0);
        check (m0.region && m1.region && m1.start > m0.start + 0.2f, "the sample page draws the region markers from sample.start / sample.end");
        r.inst.set ("osc1.type", 0.0f);
        r.inst.set ("osc1.remap.on", 1.0f);
        check (OscMarks::of (r.inst, 2, 16).remap && ! OscMarks::of (r.inst, 2, 17).remap, "the map page draws the remap curve, only while the remap is on");
        // the picture of the page changes when the region does
        auto hash = [&] (Rig& g) { auto d = std::make_unique<gpu::SceneData>(); gpu::SceneBuilder sb (*d); sb.begin (gpu::kKindPlay, 0, 1280, 800); g.page.draw (sb); return page::sceneHash (*d); };
        Rig a (2), b (2);
        a.inst.set ("osc1.type", 1.0f); b.inst.set ("osc1.type", 1.0f);
        a.inst.contentPtr = content; b.inst.contentPtr = content;
        b.inst.set ("osc1.sample.start", 40.0f);
        a.refresh(); b.refresh();
        check (hash (a) != hash (b), "the picture of a sample page follows the start of the region");
    }
    {
        Rig r (2);   // Osc A
        OscUi& u = r.page.ui();
        check (u.isPage() && u.selection() == 2 && r.page.selection() == 2, "the page of Osc A shows oscillator A although the Play selector says B");
        check (u.full().isPage() && ! hasLeaf (u.full(), &u.full().close), "the full screen is a page: no CLOSE button on it");
        check (r.inst.fullOpen && r.stack.element() == OscPageUi::kElementPage && r.stack.depth() == 1, "the page is the full screen of the oscillator UI (the host widgets are hidden)");
        check (r.inst.q[0] == Inst::idx ("osc1.type") && r.inst.q[2] == Inst::idx ("osc1.frame") && synthyboy_mpc::QRouter::indexOf (r.inst.q[3]) == Inst::idx ("osc1.transpose"), "the Basic group of osc1 [TYPE, TABLE, WT POS, OCT] is the Q group on arrival");
        check (r.page.display().valid() && r.page.display().kind() == synthyboy::OscCycle::Kind::wavetable && ! u.displayRect().empty(), "the display of the oscillator has its data and its rectangle");
        // the position knob of the page writes osc1.frame and nothing of the Play selector; level, pan and destination are read-only here (the indicator takes no touch)
        const ui::Rect lr = u.full().top.pos.rect();
        const float before = r.inst.get ("osc1.frame");
        r.drag (lr.x + lr.w / 2, lr.y + lr.h / 2, lr.x + lr.w / 2, lr.y + lr.h / 2 - 40);
        check (r.inst.get ("osc1.frame") > before + 5.0f && r.inst.selWrites == 0 && r.inst.sel == 3, "a drag on WT POS writes osc1.frame; the Play selector is not written");
        {
            const ui::Rect mx = u.full().mix.rect();
            const float lv0 = r.inst.get ("osc1.level"), pn0 = r.inst.get ("osc1.pan"), ds0 = r.inst.get ("osc1.destination");
            r.drag (mx.x + mx.w / 2, mx.y + mx.h / 2, mx.x + mx.w / 2, mx.y + mx.h / 2 - 40);
            r.tap (mx.x + mx.w / 2, mx.y + mx.h / 2);
            check (r.inst.get ("osc1.level") == lv0 && r.inst.get ("osc1.pan") == pn0 && r.inst.get ("osc1.destination") == ds0 && r.page.topPage() == 0, "the level / pan / destination indicator is read-only: a tap and a drag on it write nothing and open nothing");
        }
        // the unison detail page opens over it and closes back to the page
        r.stack.openDetail (OscUi::kPageUnison);
        r.refresh();
        check (r.page.topPage() == OscUi::kPageUnison && ! u.displayRect().empty(), "the unison page opens over the page (the display stays)");
        r.stack.back();
        r.refresh();
        check (r.page.topPage() == 0 && r.stack.depth() == 1 && r.stack.element() == OscPageUi::kElementPage, "back: the page again");
        // the scene of the page: the ground is the host's, the picture is one scene
        gpu::SceneData sd;
        gpu::SceneBuilder sb (sd);
        sb.begin (gpu::kKindPlay, 0, 1280, 800);
        page::ground (sb);
        r.page.draw (sb);
        check (! sb.overflow && sd.numOps > 100, "the picture of the page is one scene (" + std::to_string (sd.numOps) + " ops)");
    }
    {
        Rig r (1);   // Sub
        check (r.page.ui().selection() == 1 && r.inst.q[1] == Inst::idx ("sub.octave") && r.inst.q[2] < 0 && r.page.display().valid(), "the Sub page: the sub oscillator, its Basic group [WAVEFORM, OCT, CRS, PHASE], its display");
        check (r.page.ui().full().instance() == ui::OscInstance::Sub, "... and the sub oscillator's full screen");
        std::string why;
        check (leavesNoOverlap (r.page.ui().full(), why), "Sub page: no control rectangle overlaps another: " + why);

        // Display keeps about half the height
        const ui::Rect disp = r.page.ui().displayRect();
        check (disp.w == 1264 && disp.h >= 240 && disp.h <= 350, "Sub page: display keeps about half height (" + std::to_string (disp.w) + "x" + std::to_string (disp.h) + ")");

        // the bottom band is ONE block (BEHAVIOUR: ENV 1, MONO, FREE RUN); WAVEFORM, OCT, CRS and PHASE are the top row
        auto& panel = r.page.ui().full().panel;
        check (panel.numBlocks() == 2 && std::string (panel.blockTitle (0)) == "TUNE" && std::string (panel.blockTitle (1)) == "BEHAVIOUR", "Sub page: ONE labelled block, BEHAVIOUR");
        const ui::Knob* crs = panel.knob (ui::PanelSlot::SubCrs);
        const ui::Knob* phase = panel.knob (ui::PanelSlot::SubPhase);
        check (crs != nullptr && phase != nullptr && crs->rect().w >= 56 && crs->rect().h >= 56 && phase->rect().w >= 56, "Sub CRS and PHASE are medium side knobs");

        // every engine parameter of Sub (14 total) is reachable: the default view (8 controls incl. POWER), the Play shortcut (level, pan, destination: compact view) and MORE (the F1 / F2 balance); the sends are the Mixer's
        const char* const kSubParams[] = {
            "sub.on", "sub.waveform", "sub.octave",
            "sub.tune", "sub.env1", "sub.mono",
            "sub.phase", "sub.phase_contiguous", "sub.filter_blend",
            // Sends remain in the Mixer; BLEND is directly on this screen.
        };
        for (const char* p : kSubParams)
        {
            const int pidx = Inst::idx (p);
            check (pidx >= 0, std::string ("Sub parameter exists in engine: ") + p);
            bool found = false;
            for (int k = 0; k < 300; ++k)
            {
                const OscUi::Item* it = r.page.ui().item (k);
                if (it != nullptr && it->param == pidx && it->c != nullptr && it->c->enabled())
                {
                    found = true;
                    check (it->c->rect().w >= 56 && it->c->rect().h >= 56, std::string ("Sub control touch target >= 56 px: ") + p);
                    break;
                }
            }
            check (found, std::string ("Sub parameter reachable on page: ") + p);
        }

        // Greyed-out-only blocks do not exist
        for (int b = 0; b < panel.numBlocks(); ++b)
            check (hasUsableControl (panel, b), std::string ("Sub block '") + panel.blockTitle (b) + "' has enabled controls, no greyed-out-only block");
        // negative control: disable the Sub block's children and verify rejection
        panel.setShown (ui::PanelSlot::SubEnv1, false);
        panel.setShown (ui::PanelSlot::SubMono, false);
        panel.setShown (ui::PanelSlot::SubContig, false);
        check (! hasUsableControl (panel, 1), "Sub BEHAVIOUR block rejected when all children are hidden");
    }
    {
        Rig r (5);   // Noise
        check (r.page.ui().selection() == 5 && r.inst.q[1] == Inst::idx ("noise.sample"), "the Noise page: the noise, its Basic group [KIND, SOUND, FILTER, STEREO]");
        check (r.page.ui().full().instance() == ui::OscInstance::Noise, "... and the noise's full screen");
        std::string why;
        check (leavesNoOverlap (r.page.ui().full(), why), "Noise page (generated mode): no control rectangle overlaps another: " + why);

        // the first Q group: KIND, SOUND, FILTER, STEREO
        check (r.inst.q[0] == Inst::idx ("noise.type") && r.inst.q[1] == Inst::idx ("noise.sample") && r.inst.q[2] < 0 && r.inst.q[3] < 0,
               "Noise Basic Q group: KIND and SOUND; character controls are below");

        // Generated noise mode: 1 TUNE block (PITCH, FINE, KEY TRACK, ENV 1), no PLAYBACK block
        auto& panel = r.page.ui().full().panel;
        check (panel.numBlocks() == 2, "Noise (generated mode): exactly 1 block (TUNE), no PLAYBACK block");
        check (std::string (panel.blockTitle (0)) == "TUNE", "Noise block title is TUNE");
        check (r.page.ui().full().top.kind.enabled() && r.page.ui().full().top.sound.enabled() && panel.part (ui::PanelSlot::NoiseStereo)->enabled(), "Noise KIND, SOUND and STEREO are enabled");
        check (panel.part (ui::PanelSlot::NoiseEnv1) != nullptr && panel.part (ui::PanelSlot::NoiseEnv1)->enabled(), "Noise ENV 1 toggle is enabled");
        check (panel.part (ui::PanelSlot::NoiseStart) == nullptr, "Noise START knob is hidden in generated mode");
        check (panel.part (ui::PanelSlot::NoiseRand) == nullptr, "Noise RANDOM toggle is hidden in generated mode");
        check (panel.part (ui::PanelSlot::NoiseLoop) == nullptr, "Noise LOOP toggle is hidden in generated mode");
        check (panel.part (ui::PanelSlot::NoiseBounce) == nullptr, "Noise BOUNCE toggle is hidden in generated mode");

        // Active noise parameters reachable in generated mode (level, pan and destination are read-only on the page: the indicator; the Mixer is their home)
        const char* const kNoiseGenParams[] = {
            "noise.on", "noise.sample", "noise.filter", "noise.pitch", "noise.fine", "noise.keytrack",
            "noise.type", "noise.stereo", "noise.env1"
        };
        for (const char* p : kNoiseGenParams)
        {
            const int pidx = Inst::idx (p);
            bool found = false;
            for (int k = 0; k < 300; ++k)
            {
                const OscUi::Item* it = r.page.ui().item (k);
                if (it != nullptr && it->param == pidx && it->c != nullptr && it->c->enabled())
                {
                    found = true;
                    check (it->c->rect().w >= 56 && it->c->rect().h >= 56, std::string ("Noise control touch target >= 56 px: ") + p);
                    break;
                }
            }
            check (found, std::string ("Noise parameter reachable on page: ") + p);
        }

        // Now switch to Sample mode: noise.type = 1.0f
        r.inst.set ("noise.type", 1.0f);
        r.inst.set ("noise.loop", 0.0f);
        r.refresh();
        check (leavesNoOverlap (r.page.ui().full(), why), "Noise page (sample mode): no control rectangle overlaps another: " + why);

        // Sample mode: 2 blocks (TUNE, PLAYBACK); SOUND does not apply to a sample: hidden
        auto& panel2 = r.page.ui().full().panel;
        check (panel2.numBlocks() == 3, "Noise (sample mode): 2 blocks (TUNE, PLAYBACK)");
        check (std::string (panel2.blockTitle (0)) == "TUNE", "Noise block 0 title is TUNE");
        check (std::string (panel2.blockTitle (2)) == "PLAYBACK", "Noise block 1 title is PLAYBACK");
        {
            ui::Component* lv[96];
            const int nl = r.page.ui().full().leaves (lv, 96);
            bool sound = false;
            for (int i = 0; i < nl; ++i) sound = sound || lv[i] == &r.page.ui().full().top.sound;
            check (! sound, "Noise (sample mode): SOUND (noise.sample) is hidden, not dimmed");
        }

        // In sample mode, all sample controls are now present and enabled
        check (panel.part (ui::PanelSlot::NoiseStart) != nullptr && panel.part (ui::PanelSlot::NoiseStart)->enabled(), "Noise START knob present and enabled in sample mode");
        check (panel.part (ui::PanelSlot::NoiseRand) != nullptr && panel.part (ui::PanelSlot::NoiseRand)->enabled(), "Noise RANDOM toggle present and enabled in sample mode");
        check (panel.part (ui::PanelSlot::NoiseLoop) != nullptr && panel.part (ui::PanelSlot::NoiseLoop)->enabled(), "Noise LOOP toggle present and enabled in sample mode");
        check (r.page.ui().full().panel.shown (ui::PanelSlot::NoiseBounce), "Noise BOUNCE shows in sample mode (loop off)");
        const ui::Rect bounce = panel.part (ui::PanelSlot::NoiseBounce)->rect();
        for (float expected : { 1.0f, 0.0f })
        {
            r.tap (bounce.x + bounce.w / 2, bounce.y + bounce.h / 2);
            check (r.inst.get ("noise.bounce") == expected && r.inst.get ("noise.loop") == 0.0f,
                   "BOUNCE toggles without changing LOOP");
        }
        r.inst.set ("noise.loop", 1.0f);
        r.refresh();
        check (r.page.ui().full().panel.shown (ui::PanelSlot::NoiseBounce), "Noise BOUNCE shows in sample mode (loop on)");

        // All 19 parameters of noise are reachable in sample mode
        const char* const kNoiseAllParams[] = {
            "noise.on", "noise.sample", "noise.filter", "noise.pitch", "noise.fine", "noise.keytrack",
            "noise.type", "noise.stereo", "noise.env1",
            "noise.phase", "noise.random_phase", "noise.loop", "noise.bounce"
        };
        for (const char* p : kNoiseAllParams)
        {
            const int pidx = Inst::idx (p);
            bool found = false;
            for (int k = 0; k < 300; ++k)
            {
                const OscUi::Item* it = r.page.ui().item (k);
                if (it != nullptr && it->param == pidx && it->c != nullptr && it->c->enabled())
                {
                    found = true;
                    break;
                }
            }
            check (found, std::string ("Noise sample parameter reachable on page: ") + p);
        }

        // Greyed-out-only blocks do not exist in sample mode either
        for (int b = 0; b < panel.numBlocks(); ++b)
            check (hasUsableControl (panel, b), std::string ("Noise block '") + panel.blockTitle (b) + "' has enabled controls, no greyed-out-only block");
    }
    {
        // the table selector of a page opens the wavetable browser (Osc A and Osc C); a built-in table is chosen as the stepper chooses it, the parameter is written
        for (int sel : { 2, 4 })
        {
            Rig r (sel);
            const std::string key = sel == 2 ? "osc1.wavetable" : "osc3.wavetable";
            ui::Stepper& st = r.page.ui().full().top.source.stepper;
            check (st.listable && r.page.ui().full().top.source.dropdown.enabled(), "the table stepper of the page is listable and its list button is enabled");
            r.tap (st.rect().x + st.rect().w / 2, st.rect().y + st.rect().h / 2);
            check (r.page.topPage() == OscUi::kPageList && std::string (r.page.ui().list().state.header) == "Basic", "page " + std::to_string (sel) + ": the name of the table opens the browser in the category of the table");
            const ui::Rect row = r.page.ui().list().rowRect (0);
            r.tap (row.x + 30, row.y + row.h / 2);
            check (r.inst.get (key) == 0.0f && r.page.topPage() == 0, "page " + std::to_string (sel) + ": a built-in table writes " + key + " and the browser closes");
        }
    }
    {
        Rig r (4);   // Osc C: the readout targets
        const auto targets = OscPageUi::readoutTargets();
        check (targets.size() == 12 && targets[0] == Inst::idx ("osc1.frame") && targets[4] == Inst::idx ("osc2.frame")
               && targets[8] == Inst::idx ("osc3.frame"), "every page can select from the three oscillator readout slices in Play order");
        check (r.page.ui().selection() == 4 && synthyboy_mpc::QRouter::indexOf (r.inst.q[3]) == Inst::idx ("osc3.transpose"), "the Osc C page is osc3");
    }
    {
        // Standalone OSC native instance choice with OFF navigation
        for (int sel = 1; sel <= 5; ++sel)
        {
            Rig r (sel);
            check (r.page.selection() == sel, "standalone page selection matches instance " + std::to_string (sel));
            check (r.page.ui().selection() == sel, "internal OscUi selection matches instance " + std::to_string (sel));
            check (r.page.ui().full().isPage(), "standalone page is in page mode");
            check (OscPageUi::isInstanceOn (r.inst, sel), "instance " + std::to_string (sel) + " is initially on in Rig");
            check (OscPageUi::choiceLabel (r.inst, sel).find ("(Active)") != std::string::npos,
                   "choiceLabel reflects (Active) state for instance " + std::to_string (sel));
        }

        // Test OFF navigation for Osc A (instance 2)
        {
            Rig r (2);
            check (std::string (r.page.ui().full().pageTitle()) == "OSC A", "initial page title is OSC A when on");
            check (r.page.ui().full().power.state.on, "osc1 power is on");

            // Power off Osc A
            r.inst.set ("sub.on", 1.0f);
            const int w0 = r.inst.writes;
            r.inst.set ("osc1.on", 0.0f);
            r.refresh();

            check (! OscPageUi::isInstanceOn (r.inst, 2), "Osc A is now recognized as OFF");
            check (OscPageUi::choiceLabel (r.inst, 2) == "Osc A (Inactive)", "choiceLabel reflects Osc A (Inactive)");
            check (std::string (r.page.ui().full().pageTitle()) == "OSC A  OFF", "page title reflects OFF navigation: OSC A  OFF");
            check (r.page.display().valid() && r.page.display().kind() == synthyboy::OscCycle::Kind::off,
                   "display shows dimmed OFF response");
            check (r.page.ui().full().power.enabled() && ! r.page.ui().full().power.inactive(),
                   "POWER toggle remains enabled and active while oscillator is OFF");
            check (r.page.ui().full().top.inactive() && r.page.ui().full().panel.inactive(),
                   "sound and parameter controls are locked in inactive state");

            // Attempting to drag sound control while OFF does not write parameters
            const ui::Rect lr = r.page.ui().full().top.pos.rect();
            if (! lr.empty())
                r.drag (lr.x + lr.w / 2, lr.y + lr.h / 2, lr.x + lr.w / 2, lr.y + lr.h / 2 - 40);
            check (r.inst.writes == w0, "dragging sound controls on OFF oscillator writes zero parameters");

            // Navigate to Sub while Osc A is OFF, then back to Osc A
            r.sel = 1;
            r.refresh();
            check (r.page.selection() == 1, "navigated to Sub");
            check (std::string (r.page.ui().full().pageTitle()) == "SUB", "Sub is ON and title is SUB");

            r.sel = 2;
            r.refresh();
            check (r.page.selection() == 2, "navigated back to Osc A");
            check (std::string (r.page.ui().full().pageTitle()) == "OSC A  OFF", "Osc A remains OFF after navigation");
            check (r.inst.writes == w0, "navigating across OFF oscillator produced zero parameter writes");

            // Power Osc A back ON
            r.inst.set ("osc1.on", 1.0f);
            r.refresh();
            check (OscPageUi::isInstanceOn (r.inst, 2), "Osc A is ON again");
            check (std::string (r.page.ui().full().pageTitle()) == "OSC A", "page title restored to OSC A");
            check (! r.page.ui().full().top.inactive() && ! r.page.ui().full().panel.inactive(),
                   "controls restored to active state upon powering ON");
        }

        // Test OFF navigation for Sub (instance 1) and Noise (instance 5)
        {
            Rig r (1);
            r.inst.set ("sub.on", 0.0f);
            r.refresh();
            check (std::string (r.page.ui().full().pageTitle()) == "SUB  OFF", "Sub title reflects SUB  OFF");
            check (OscPageUi::choiceLabel (r.inst, 1) == "Sub (Inactive)", "Sub choiceLabel is Sub (Inactive)");
        }
        {
            Rig r (5);
            r.inst.set ("noise.on", 0.0f);
            r.refresh();
            check (std::string (r.page.ui().full().pageTitle()) == "NOISE  OFF", "Noise title reflects NOISE  OFF");
            check (OscPageUi::choiceLabel (r.inst, 5) == "Noise (Inactive)", "Noise choiceLabel is Noise (Inactive)");
        }

        // Standalone OSC native selector aperture and OFF navigation integration
        {
            Rig r (2);
            check (! r.page.hasNativeInstanceSelector(), "initially native instance selector is false");
            check (! r.page.hostBackground(), "initially hostBackground is false");

            r.page.setNativeInstanceSelector (true);
            r.refresh();

            check (r.page.hasNativeInstanceSelector(), "setNativeInstanceSelector(true) enables native selector");
            check (! r.page.hostBackground(), "the native selector alone does not confirm the host background");
            check (r.page.ui().hasNativeInstanceSelector(), "internal OscUi reflects native selector state");

            check (r.page.selectorArea().empty(), "a page of a tab has no in-screen source strip: the hardware sub-tab selects the oscillator");
            check (r.page.covers (500, 300), "normal page area is covered by overlay");

            // Scene drawing with aperture
            gpu::SceneData sd;
            gpu::SceneBuilder sb (sd);
            sb.begin (gpu::kKindPlay, 0, 1280, 800);
            r.page.draw (sb);
            check (! sb.overflow, "scene of the page without a source strip draws without overflow");

            // OFF navigation with native selector active
            const int w0 = r.inst.writes;
            r.inst.set ("osc1.on", 0.0f);
            r.refresh();

            check (! OscPageUi::isInstanceOn (r.inst, 2), "Osc A is OFF");
            check (r.page.ui().full().power.enabled() && ! r.page.ui().full().power.inactive(),
                   "POWER toggle remains enabled and active while oscillator is OFF");
            check (! r.page.ui().full().close.inactive(),
                   "close / navigation remains active (inactive=false) while oscillator is OFF");

            // Navigate across OFF oscillator to Sub
            r.sel = 1;
            r.refresh();
            check (r.page.selection() == 1, "navigated to Sub while Osc A was OFF");
            check (r.inst.writes == w0, "navigating across OFF oscillator produced zero parameter writes");

            // Navigate back to Osc A
            r.sel = 2;
            r.refresh();
            check (r.page.selection() == 2, "navigated back to Osc A");
            check (std::string (r.page.ui().full().pageTitle()) == "OSC A  OFF", "Osc A remains OFF after navigation");
            check (r.inst.writes == w0, "zero parameter writes during return navigation");

            // Power Osc A back ON
            r.inst.set ("osc1.on", 1.0f);
            r.refresh();
            check (OscPageUi::isInstanceOn (r.inst, 2), "Osc A is ON again");
            check (std::string (r.page.ui().full().pageTitle()) == "OSC A", "page title restored to OSC A");
            check (! r.page.ui().full().top.inactive() && ! r.page.ui().full().panel.inactive(),
                   "controls restored to active state upon powering ON");

            // Native TUNE aperture, input pass-through, and OFF / modal protection
            {
                const bool prevSettings = r.inst.useNativeSettings;
                const bool prevBg = r.page.hostBackground();
                r.inst.useNativeSettings = true;
                r.page.setHostBackground (true);
                r.refresh();
                const ui::Rect tApt = OscPageUi::tuneArea();
                check (tApt.x == 994 && tApt.y == 312 && tApt.w == 278 && tApt.h == 154,
                       "tuneArea matches approved 278x154 card geometry at (994, 312)");
                check (r.page.isNativeTuneActive(), "native tune is active on Osc A while ON");
                const int tcx = tApt.x + tApt.w / 2, tcy = tApt.y + tApt.h / 2;
                check (! r.page.covers (tcx, tcy), "centre of tuneArea is not covered by overlay while ON");
                check (! r.page.covers (tApt.x + 8, tApt.y + 8), "corner of tuneArea is not covered by overlay while ON");
                check (! r.page.touch (ev (TouchEvent::Down, tcx, tcy), r.stack, r.inst, 100.0),
                       "touch down inside tuneArea returns false (passed to host) while ON");
                check (! r.page.wheel (tcx, tcy, 1, r.stack, r.inst, 100.0),
                       "wheel inside tuneArea returns false (passed to host) while ON");

                // When oscillator is turned OFF
                r.inst.set ("osc1.on", 0.0f);
                r.refresh();
                check (r.page.isNativeTuneActive(), "native tune receives its inactive presentation when the oscillator is OFF");
                check (! r.page.covers (tcx, tcy), "inactive native TUNE remains visible when OFF");

                // Restore Osc A ON
                r.inst.set ("osc1.on", 1.0f);
                r.refresh();
                check (r.page.isNativeTuneActive(), "native tune restored when oscillator is turned back ON");

                // Inactive on Sub (sel=1) and Noise (sel=5)
                r.sel = 1;
                r.refresh();
                check (! r.page.isNativeTuneActive(), "native tune is inactive on Sub (sel=1)");
                check (r.page.covers (tcx, tcy), "tuneArea is covered by overlay on Sub");

                r.sel = 5;
                r.refresh();
                check (! r.page.isNativeTuneActive(), "native tune is inactive on Noise (sel=5)");
                check (r.page.covers (tcx, tcy), "tuneArea is covered by overlay on Noise");

                // Active on Osc B (sel=3) and Osc C (sel=4) when powered ON
                r.inst.set ("osc2.on", 1.0f);
                r.sel = 3;
                r.refresh();
                check (r.page.isNativeTuneActive(), "native tune is active on Osc B (sel=3)");
                check (! r.page.covers (tcx, tcy), "tuneArea is open on Osc B");

                r.inst.set ("osc3.on", 1.0f);
                r.sel = 4;
                r.refresh();
                check (r.page.isNativeTuneActive(), "native tune is active on Osc C (sel=4)");
                check (! r.page.covers (tcx, tcy), "tuneArea is open on Osc C");

                // Return to Osc A (sel=2)
                r.sel = 2;
                r.page.setHostBackground (prevBg);
                r.inst.useNativeSettings = prevSettings;
                r.refresh();
            }
        }

        // Each physical tab routes its native POWER button through the selected source's current binding.
        {
            namespace n = nativeui::osc;
            static const char* const keys[] { "sub.on", "osc1.on", "osc2.on", "osc3.on", "noise.on" };
            for (int source = 1; source <= n::kLastSource; ++source)
            {
                Rig r (source);
                r.page.setHostBackground (true);
                r.refresh();
                const int other = source % n::kLastSource + 1;
                const float unchanged = r.inst.get (keys[other - 1]);
                const int before = r.inst.writes;
                const auto power = r.page.powerArea();
                const int x = power.x + power.w / 2, y = power.y + power.h / 2;
                const auto expectedPower = source >= 2 && source <= 4 ? ui::Rect {54, page::kTabY + 8, 64, 56}
                    : ui::playrows::powerRect (r.page.ui().full().displayRect());
                check (power == expectedPower && ! r.page.covers (x, y)
                       && ! hasLeaf (r.page.ui().full(), &r.page.ui().full().power) && r.inst.native.snapshot().powerValue (source) == 1.0f,
                       std::string (keys[source - 1]) + " starts in the exclusive host POWER aperture");
                check (! r.page.touch (ev (TouchEvent::Down, x, y), r.stack, r.inst, r.now)
                       && ! r.page.wheel (x, y, 1, r.stack, r.inst, r.now), "host owns POWER hit and wheel");
                r.inst.native.postPower (source, 0.0f);
                r.inst.native.postPower (other, unchanged >= 0.5f ? 0.0f : 1.0f);
                r.refresh();
                check (r.inst.writes == before + 1 && r.inst.lastWritten == Inst::idx (keys[source - 1])
                       && r.inst.get (keys[other - 1]) == unchanged && r.inst.native.snapshot().powerValue (source) == 0.0f
                       && r.page.ui().full().inactive() && ! r.page.covers (x, y)
                       && ! r.page.isNativeRoutingActive(),
                       std::string (keys[source - 1]) + " turns OFF once and keeps only POWER native");
                r.inst.native.post (n::kRouting, 0.5f);
                r.inst.native.postPower (source, 0.0f);   // host echo
                r.refresh();
                check (r.inst.writes == before + 1, "OFF routing and the power echo do not write");
                r.inst.native.postPower (source, 1.0f);
                r.refresh();
                check (r.inst.writes == before + 2 && r.inst.native.snapshot().powerValue (source) == 1.0f
                       && ! r.page.ui().full().inactive() && r.page.isNativeRoutingActive(),
                       std::string (keys[source - 1]) + " turns ON once and reopens routing");
            }

            Rig r (2);
            r.page.setHostBackground (true);
            r.inst.set ("osc2.on", 0.0f);
            r.refresh();
            const int before = r.inst.writes;
            r.inst.native.postPower (2, 0.0f);
            r.inst.native.post (n::kSource, 0.5f);   // choose Osc B before the pending A power command is read
            r.refresh();
            check (r.page.selection() == 3 && r.inst.get ("osc1.on") == 1.0f && r.inst.get ("osc2.on") == 0.0f
                   && r.inst.writes == before, "a source change expires the previous source's queued POWER");
            r.inst.native.postPower (3, 1.0f);
            ++r.inst.generation;
            r.refresh();
            check (r.page.selection() == 2 && r.inst.get ("osc2.on") == 0.0f && r.inst.writes == before,
                   "preset recall resets the default tab and expires pending POWER");
            r.inst.native.postPower (2, 0.0f);
            const auto more = r.page.ui().full().more.rect();
            r.tap (more.x + more.w / 2, more.y + more.h / 2);
            check (r.page.topPage() != 0 && r.inst.native.snapshot().visibility() == n::kHidden && r.inst.writes == before,
                   "opening the real detail button isolates the modal from queued POWER");
            r.inst.native.postPower (2, 0.0f);
            r.stack.back();
            r.refresh();
            check (r.page.topPage() == 0 && r.inst.get ("osc1.on") == 1.0f && r.inst.writes == before,
                   "a POWER command made inside the modal cannot replay on dismissal");
            r.inst.native.postPower (2, 0.0f);
            r.page.leftTab (r.inst);
            r.refresh();
            check (r.inst.get ("osc1.on") == 1.0f && r.inst.writes == before,
                   "tab leave and reopen discard queued POWER on the same source");
        }

        // Standalone OSC native routing destination menu, dual apertures, OFF protection, and modal isolation
        {
            static const char* const kExpectedKeys[playosc::kInstances] = {
                "sub.destination", "osc1.destination", "osc2.destination", "osc3.destination", "noise.destination"
            };
            static const char* const kExpectedChoices[OscPageUi::kDestinationChoiceCount] = {
                "Filter 1", "Filter 2", "Filters 1+2", "Effects only", "Direct out", "Filter blend", "None"
            };

            // 1. Per-source parameter bindings and all 7 valid destination choices
            for (int sel = 1; sel <= playosc::kInstances; ++sel)
            {
                Rig r (sel);
                check (std::string (OscPageUi::destinationKey (sel)) == kExpectedKeys[sel - 1],
                       "destinationKey(" + std::to_string (sel) + ") matches " + kExpectedKeys[sel - 1]);
                const int pidx = OscPageUi::destinationParamIndex (sel);
                check (pidx >= 0 && pidx == Inst::idx (kExpectedKeys[sel - 1]),
                       "destinationParamIndex resolves " + std::string (kExpectedKeys[sel - 1]));
                const synthyboy::ParamInfo& pi = synthyboy::paramTable()[pidx];
                bool choicesOk = pi.numChoices == OscPageUi::kDestinationChoiceCount;
                for (int c = 0; c < OscPageUi::kDestinationChoiceCount && choicesOk; ++c)
                    choicesOk = std::string (OscPageUi::destinationChoices()[c]) == kExpectedChoices[c]
                        && pi.choices[c] != nullptr && std::string (pi.choices[c]) == kExpectedChoices[c];
                check (choicesOk, "source " + std::to_string (sel) + " exposes the 7 valid destination choices");

                for (int choice = 0; choice < OscPageUi::kDestinationChoiceCount; ++choice)
                {
                    r.inst.writeParam (pidx, static_cast<float> (synthyboy::toNormalised (pi, static_cast<float> (choice))));
                    r.refresh();
                    check (OscPageUi::currentDestination (r.inst, sel) == choice,
                           "source " + std::to_string (sel) + " destination tracks choice " + std::to_string (choice));
                }
            }

            // Helper to measure alpha bounds of rendered pixels in a canvas rectangle
            auto renderPage = [] (const OscPageUi& p, std::vector<uint32_t>& cv)
            {
                auto sd = std::make_unique<gpu::SceneData>();
                gpu::SceneBuilder sb (*sd);
                sb.begin (gpu::kKindPlay, 0, 1280, 800);
                p.draw (sb);
                const bool overflow = sb.overflow;
                renderScene (*sd, cv);
                return ! overflow;
            };
            auto alphaRange = [] (const std::vector<uint32_t>& cv, const ui::Rect& rect, uint32_t& minA, uint32_t& maxA)
            {
                minA = 255u;
                maxA = 0u;
                for (int y = std::max (0, rect.y); y < std::min (800, rect.bottom()); ++y)
                    for (int x = std::max (0, rect.x); x < std::min (1280, rect.right()); ++x)
                    {
                        const uint32_t a = (cv[static_cast<size_t> (y) * 1280u + static_cast<size_t> (x)] >> 24) & 255u;
                        minA = std::min (minA, a);
                        maxA = std::max (maxA, a);
                    }
            };

            // 2. Dual-aperture coexistence, pass-through, pixel transparency, and cancellation / zero-write preservation
            Rig r (2);
            r.page.setHostBackground (true);
            r.refresh();
            check (r.page.hasNativeInstanceSelector() && r.page.hasNativeRouting() && r.page.isNativeRoutingActive(),
                   "setHostBackground enables the standalone native page layout and routing apertures when ON");
            check (! r.inst.fullOpen, "standalone native base page keeps fullOpen false (host controls visible)");
            check (r.page.selectorArea().empty(), "the standalone page has no source strip aperture");
            const ui::Rect sApt = r.page.ui().full().panArea();   // the bottom native row stands in for the former strip: PAN
            const ui::Rect pApt = r.page.powerArea();
            const ui::Rect rApt = r.page.routingArea();
            check (sApt.y == 690 && sApt.h == 48, "the bottom native row (PAN, LEVEL) takes the freed bottom 48 px (y 690)");
            check (rApt.x == 1122 && rApt.y == 147 && rApt.w == 150 && rApt.h == 48, "routing aperture placed at (1122, 147, 150, 48)");
            check (! sApt.overlaps (rApt), "selector and routing apertures do not overlap");

            const int rcx = rApt.x + rApt.w / 2, rcy = rApt.y + rApt.h / 2;
            const int scx = sApt.x + sApt.w / 2, scy = sApt.y + sApt.h / 2;
            check (! r.page.covers (scx, scy) && ! r.page.covers (rcx, rcy), "both PAN and routing apertures pass through on base page");
            check (r.page.covers (640, rcy), "region between apertures remains covered by overlay");
            check (! r.page.touch (ev (TouchEvent::Down, rcx, rcy), r.stack, r.inst, r.now), "touch inside routing aperture returns false");
            check (! r.page.wheel (rcx, rcy, 1, r.stack, r.inst, r.now), "wheel inside routing aperture returns false");
            check (! r.page.wheel (scx, scy, 1, r.stack, r.inst, r.now), "wheel inside the PAN aperture returns false");

            // Cancelling (no host write committed) leaves destination untouched
            const int wBefore = r.inst.writes;
            const float destBefore = r.inst.get ("osc1.destination");
            r.page.touch (ev (TouchEvent::Move, rcx, rcy), r.stack, r.inst, r.now);
            r.page.touch (ev (TouchEvent::Up, rcx, rcy), r.stack, r.inst, r.now);
            r.refresh();
            check (r.inst.writes == wBefore && r.inst.get ("osc1.destination") == destBefore,
                   "cancelling / releasing without host write leaves destination unchanged with zero writes");

            std::vector<uint32_t> cv;
            check (renderPage (r.page, cv), "dual-aperture standalone scene builds without overflow");
            uint32_t minA = 255u, maxA = 0u;
            alphaRange (cv, sApt, minA, maxA);
            check (maxA == 0u, "selector aperture is 100% transparent on base page (maxAlpha=" + std::to_string (maxA) + ")");
            alphaRange (cv, pApt, minA, maxA);
            check (maxA == 0u, "POWER aperture is completely transparent on the native ON root");
            alphaRange (cv, rApt, minA, maxA);
            check (maxA == 0u, "routing aperture is 100% transparent on ON base page (maxAlpha=" + std::to_string (maxA) + ")");
            alphaRange (cv, { 600, 150, 80, 40 }, minA, maxA);
            check (minA == 0u, "the confirmed host background owns the static top band between the controls");

            // 3. Power-OFF closes routing aperture (while keeping selector aperture open)
            const int wOff0 = r.inst.writes;
            r.inst.set ("osc1.on", 0.0f);
            r.refresh();
            check (! r.page.isNativeRoutingActive(), "powering Osc A OFF closes routing aperture");
            check (r.page.covers (rcx, rcy), "routing aperture is covered by overlay while OFF");
            check (r.page.touch (ev (TouchEvent::Down, rcx, rcy), r.stack, r.inst, r.now),
                   "touch inside routing aperture is absorbed by overlay when OFF");
            r.page.touch (ev (TouchEvent::Up, rcx, rcy), r.stack, r.inst, r.now);
            r.refresh();
            check (r.inst.writes == wOff0, "tapping closed OFF routing aperture writes zero parameters");
            check (renderPage (r.page, cv), "OFF dual-aperture scene renders cleanly");
            alphaRange (cv, pApt, minA, maxA);
            check (maxA == 0u, "POWER aperture remains transparent on the fallback OFF root");
            alphaRange (cv, rApt, minA, maxA);
            check (maxA == 0u, "routing aperture shows the inactive native readout when OFF");

            // Sub and Noise OFF routing closure check too
            for (const std::pair<int, const char*> offCase : { std::make_pair (1, "sub.on"), std::make_pair (5, "noise.on") })
            {
                Rig rOff (offCase.first);
                rOff.page.setHostBackground (true);
                rOff.inst.set (offCase.second, 0.0f);
                rOff.refresh();
                check (! rOff.page.isNativeRoutingActive() && rOff.page.covers (rcx, rcy),
                       std::string (offCase.second) + " OFF closes routing aperture");
            }

            // Restore Osc A ON
            r.inst.set ("osc1.on", 1.0f);
            r.refresh();
            check (r.page.isNativeRoutingActive() && ! r.page.covers (rcx, rcy), "powering Osc A ON restores transparent routing aperture");

            // 4. Modal isolation across all standalone OSC modal pages
            const int wModal0 = r.inst.writes;
            const int modalIds[] = {
                oscspec::kMore,
                oscspec::kOptions,
                OscUi::kPageUnison,
                OscUi::kPageMods,
                OscUi::kPageRoute,
            };
            for (int modalId : modalIds)
            {
                r.stack.openDetail (modalId);
                r.refresh();
                check (r.page.topPage() == modalId, "modal " + std::to_string (modalId) + " opened");
                check (r.inst.fullOpen, "modal " + std::to_string (modalId) + " drives fullOpen=true");
                check (r.page.covers (scx, scy) && r.page.covers (rcx, rcy),
                       "modal " + std::to_string (modalId) + " covers both selector and routing apertures");
                check (renderPage (r.page, cv), "modal " + std::to_string (modalId) + " renders without overflow");
                alphaRange (cv, sApt, minA, maxA);
                check (minA == 255u, "modal " + std::to_string (modalId) + " paints opaque ground over selector aperture");
                alphaRange (cv, pApt, minA, maxA);
                check (minA == 255u && r.page.covers (pApt.x + pApt.w / 2, pApt.y + pApt.h / 2),
                       "modal " + std::to_string (modalId) + " owns the POWER aperture");
                alphaRange (cv, rApt, minA, maxA);
                check (minA == 255u, "modal " + std::to_string (modalId) + " paints opaque ground over routing aperture");

                // Dismiss via actual modal controls (including BACK overlapping selectorArea_ or backdrop inside routingArea_)
                if (modalId == OscUi::kPageRoute)
                {
                    r.inst.useNativeDetail = true;
                    r.refresh();
                    r.inst.postDetail (nativeui::oscdetail::kBack);
                    r.refresh();
                }
                else
                {
                    const ui::Rect backRect = modalId == OscUi::kPageMods ? r.page.ui().modsPage().back.rect()
                                            : modalId == OscUi::kPageUnison ? r.page.ui().detail().back.rect()
                                            : r.page.ui().specPage().back.rect();
                    r.tap (backRect.x + backRect.w / 2, backRect.y + backRect.h / 2);
                }
                check (r.page.topPage() == 0 && ! r.inst.fullOpen,
                       "modal " + std::to_string (modalId) + " closes back to base page and restores fullOpen=false");
            }

            // Also verify List/Browser popup modal isolation via wavetable source stepper
            {
                ui::Stepper& st = r.page.ui().full().top.source.stepper;
                r.tap (st.rect().x + st.rect().w / 2, st.rect().y + st.rect().h / 2);
                check (r.page.topPage() == OscUi::kPageList && r.inst.fullOpen, "List popup opens as modal with fullOpen=true");
                check (r.page.covers (scx, scy) && r.page.covers (rcx, rcy), "List popup covers both apertures");
                check (renderPage (r.page, cv), "List popup renders cleanly");
                alphaRange (cv, sApt, minA, maxA);
                check (minA == 255u, "List popup paints opaque ground over selector aperture");
                alphaRange (cv, rApt, minA, maxA);
                check (minA == 255u, "List popup paints opaque ground over routing aperture");
                r.tap (scx, scy);   // backdrop inside selector aperture must consume touch and dismiss popup
                check (r.page.topPage() == 0 && ! r.inst.fullOpen, "tapping selector aperture while List is open dismisses modal cleanly");
            }
            check (r.inst.writes == wModal0, "opening and dismissing all modals wrote zero parameters");
            check (! r.page.covers (scx, scy) && ! r.page.covers (rcx, rcy), "returning from modals restores both base-page apertures");
        }

        // 5. Long-press modulation on Mix PAN/LEVEL, TUNE card (OCT/SEM/FIN/CRS), and Unison VOICES,
        // plus WARP 1 / WARP 2 captions and Noise FINE aperture coverage.
        {
            Rig r (2);
            r.page.setHostBackground (true);
            r.page.setNativeInstanceSelector (true);
            r.page.setNativeRouting (true);
            r.inst.useNativeSettings = true;
            r.inst.set ("osc1.on", 1.0f);
            r.inst.set ("osc1.type", 0.0f);
            r.inst.set ("osc1.warp.type", 1.0f);
            r.inst.set ("osc1.warp2.type", 1.0f);
            r.refresh();

            // Check WARP 1 / WARP 2 captions are non-empty
            const auto fields = r.page.ui().nativeSettingsItems();
            const int w1Slot = nativeui::osc::abcSettingIndex (2, nativeui::osc::AbcWarp1Type);
            const int w2Slot = nativeui::osc::abcSettingIndex (2, nativeui::osc::AbcWarp2Type);
            check (fields[w1Slot] != nullptr && std::string (OscUi::nativeSettingsLabel (*fields[w1Slot])) == "WARP 1",
                   "WARP 1 native label is non-empty");
            check (fields[w2Slot] != nullptr && std::string (OscUi::nativeSettingsLabel (*fields[w2Slot])) == "WARP 2",
                   "WARP 2 native label is non-empty");

            const auto checkLongPress = [&] (int x, int y, const std::string& expectedKey, const std::string& msg)
            {
                r.now += 50.0;
                r.page.touch (ev (TouchEvent::Down, x, y), r.stack, r.inst, r.now);
                r.now += 650.0;
                r.page.tick (r.now, r.stack, r.inst);
                r.refresh();
                const bool opened = r.page.topPage() == OscUi::kPageMods;
                const int target = Inst::idx (expectedKey);
                const bool rightTarget = opened && r.page.ui().modsTargetParam() == target;
                check (rightTarget, msg);
                if (opened)
                {
                    r.stack.back();
                    r.refresh();
                }
                r.now += 20.0;
                r.page.touch (ev (TouchEvent::Up, x, y), r.stack, r.inst, r.now);
                r.refresh();
            };

            const auto panR = r.page.ui().full().panArea();
            const auto lvlR = r.page.ui().full().levelArea();
            const auto routeR = r.page.ui().full().routingArea();
            const auto tuneR = OscPageUi::tuneArea();

            checkLongPress (panR.x + panR.w / 2, panR.y + panR.h / 2, "osc1.pan", "long-press PAN opens osc1.pan mods");
            checkLongPress (lvlR.x + lvlR.w / 2, lvlR.y + lvlR.h / 2, "osc1.level", "long-press LEVEL opens osc1.level mods");
            checkLongPress (tuneR.x + 70, tuneR.y + 40, "osc1.transpose", "long-press OCT opens osc1.transpose mods");
            checkLongPress (tuneR.x + 205, tuneR.y + 40, "osc1.transpose", "long-press SEM opens osc1.transpose mods");
            checkLongPress (tuneR.x + 70, tuneR.y + 76, "osc1.tune", "long-press FIN opens osc1.tune mods");
            checkLongPress (tuneR.x + 205, tuneR.y + 76, "osc1.pitch_coarse", "long-press CRS opens osc1.pitch_coarse mods");

            // VOICES (T::UpDown) in Unison cell
            if (const auto* voicesItem = r.page.ui().item (OscUi::kUnisonCount); voicesItem && voicesItem->c)
            {
                const auto vr = voicesItem->c->rect();
                checkLongPress (vr.x + vr.w / 2, vr.y + vr.h / 2, "osc1.unison.voices", "long-press VOICES opens osc1.unison.voices mods");
            }

            // ROUTE, TRACK, MORE must not open Mods on long-press
            for (auto pt : std::array<std::pair<int, int>, 3> {{{routeR.x + routeR.w / 2, routeR.y + routeR.h / 2},
                                                              {tuneR.x + 70, tuneR.y + 120},
                                                              {tuneR.x + 205, tuneR.y + 120}}})
            {
                r.now += 50.0;
                r.page.touch (ev (TouchEvent::Down, pt.first, pt.second), r.stack, r.inst, r.now);
                r.now += 650.0;
                r.page.tick (r.now, r.stack, r.inst);
                r.refresh();
                check (r.page.topPage() == 0, "long-press non-modulatable native button does not open mods");
                r.page.touch (ev (TouchEvent::Up, pt.first, pt.second), r.stack, r.inst, r.now);
                r.refresh();
            }

            // Preset change cancels pending long-press without opening Mods on the new preset/oscillator
            r.now += 50.0;
            r.page.touch (ev (TouchEvent::Down, panR.x + panR.w / 2, panR.y + panR.h / 2), r.stack, r.inst, r.now);
            ++r.inst.generation;
            r.now += 650.0;
            r.page.tick (r.now, r.stack, r.inst);
            r.refresh();
            check (r.page.topPage() == 0, "preset stateGeneration change cancels pending native long-press");
            r.page.touch (ev (TouchEvent::Up, panR.x + panR.w / 2, panR.y + panR.h / 2), r.stack, r.inst, r.now);
            r.refresh();
        }
    }
    std::printf (g_fails ? "osc_page_host: FAILED (%d)\n" : "osc_page_host: OK\n", g_fails);
    return g_fails ? 1 : 0;
}
