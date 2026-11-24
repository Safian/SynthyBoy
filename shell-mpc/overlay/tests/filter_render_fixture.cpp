// SynthyBoy - C++ Filter overlay render fixture
// Renders the real FilterUi + FilterDisplay + scene_cpu.h overlay and dumps matching host-native parameter/handle state
// for documented synthetic fixtures.
// NOTE: Synthetic test fixture for offline verification, NOT a live device capture.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "shell-mpc/native_filter_controls.h"
#include "shell-mpc/native_ui_params.h"
#include "shell-mpc/overlay/filter_ui.h"
#include "shell-mpc/overlay/native_marker_covers.h"
#include "shell-mpc/overlay/page_view.h"
#include "shell-mpc/overlay/pages.h"
#include "shell-mpc/overlay/scene_cpu.h"
#include "shell-mpc/overlay/tests/fakes.h"
#include "shell-mpc/ui/choice_names.h"
#include "shell-mpc/ui/tests/ui_test.h"
#include "synthyboy/filter_response.h"
#include "synthyboy/params.h"
#include "synthyboy/user_content.h"

using namespace synthyboy_mpc;
using namespace synthyboy_mpc::overlay;

namespace {

class FilterFixtureInstance final : public IInstanceView
{
public:
    FilterFixtureInstance()
    {
        const synthyboy::ParamInfo* t = synthyboy::paramTable();
        for (size_t i = 0; i < synthyboy::paramCount(); ++i)
            v_[i] = t[i].def;
        contentPtr = std::make_shared<const synthyboy::StateContent> (synthyboy::StateContent {});
        initLiveValues();
    }

    void initLiveValues()
    {
        haveLive = true;
        liveVal.stripPeak[0] = 0.5f;
        liveVal.stripPeak[1] = 0.72f;
        liveVal.stripPeak[2] = 0.64f;
    }

    float param (int i) const noexcept override
    {
        return (i >= 0 && static_cast<size_t> (i) < synthyboy::kMaxParams) ? v_[i] : 0.0f;
    }
    uint64_t stateGeneration() const noexcept override { return generation; }
    bool live (synthyboy::LiveValues& out) const noexcept override
    {
        out = liveVal;
        return haveLive;
    }
    double sampleRate() const noexcept override { return 48000.0; }
    double bpm() const noexcept override { return 120.0; }
    int playSelection (int g) const noexcept override { return sel[std::clamp (g, 0, 2)]; }
    void setPlaySelection (int g, int i) noexcept override { sel[std::clamp (g, 0, 2)] = i; }
    int playLastSelector() const noexcept override { return 2; }
    void setQGroup (const int* t, int n) noexcept override
    {
        for (int i = 0; i < 16; ++i) q[i] = i < n ? t[i] : -1;
    }
    void writeParam (int i, float norm) noexcept override
    {
        if (i >= 0 && static_cast<size_t> (i) < synthyboy::paramCount())
        {
            const synthyboy::ParamInfo& p = synthyboy::paramTable()[i];
            v_[i] = synthyboy::constrainValue (p, synthyboy::fromNormalised (p, norm));
        }
    }
    int tabView (int what) const noexcept override
    {
        return what == kTabViewFilter ? currentView : (what >= 0 && what < kTabViewFields ? tv[what] : 0);
    }
    void setTabView (int what, int value) noexcept override
    {
        if (what == kTabViewFilter) currentView = value;
        else if (what >= 0 && what < kTabViewFields) tv[what] = value;
    }
    void setUiFullOpen (bool o) noexcept override { fullOpen = o; }
    std::shared_ptr<const synthyboy::StateContent> content() const override { return contentPtr; }

    void setNativeFilterControls (int view, const nativeui::FxCell* cells, const float* values, const int* indicators) noexcept override
    {
        publishedView = view;
        nativeFilter.publish (view, cells);
        if (values != nullptr)
        {
            for (int i = 0; i < 2 * nativeui::kFilterCurveFields; ++i)
                publishedValues[i] = values[i];
        }
        if (indicators != nullptr)
        {
            for (int i = 0; i < nativeui::kFilterIndicators; ++i)
                publishedIndicators[i] = indicators[i];
        }
    }

    bool takeNativeFilterSlot (int slot, const nativeui::FxCell& binding, float& value) noexcept override
    {
        return nativeFilter.take (slot, binding, value);
    }

    static int idx (const std::string& key)
    {
        const auto* p = synthyboy::findParamByKey (key.c_str());
        return p != nullptr ? static_cast<int> (p - synthyboy::paramTable()) : -1;
    }

    void set (const std::string& key, float val)
    {
        const int i = idx (key);
        if (i >= 0 && static_cast<size_t> (i) < synthyboy::paramCount())
        {
            const auto& pi = synthyboy::paramTable()[i];
            v_[i] = synthyboy::constrainValue (pi, val);
        }
    }

    void mod (int slot, int src, const std::string& dest, float amt, bool bi = false, bool bypass = false)
    {
        char k[24];
        std::snprintf (k, sizeof k, "mod%02d.", slot);
        set (std::string (k) + "source", static_cast<float> (src));
        set (std::string (k) + "destination", static_cast<float> (synthyboy::modDestinationChoiceOf (static_cast<size_t> (idx (dest)))));
        set (std::string (k) + "amount", amt);
        set (std::string (k) + "bipolar", bi ? 1.0f : 0.0f);
        set (std::string (k) + "bypass", bypass ? 1.0f : 0.0f);
    }

    float v_[synthyboy::kMaxParams] {};
    synthyboy::LiveValues liveVal {};
    bool haveLive = false;
    uint64_t generation = 1;
    int sel[3] = { 1, 1, 1 };
    int q[16] = {};
    int tv[kTabViewFields] = {};
    int currentView = 0; // 0: F1, 1: F2, 2: NOTE, 3: VELO
    bool fullOpen = true;
    std::shared_ptr<const synthyboy::StateContent> contentPtr;

    nativeui::FilterCellBank nativeFilter;
    int publishedView = 0;
    float publishedValues[2 * nativeui::kFilterCurveFields] {};
    int publishedIndicators[nativeui::kFilterIndicators] {};
};

uint32_t alphaOver (uint32_t dst, uint32_t src) noexcept
{
    const uint32_t sa = (src >> 24) & 255u;
    if (sa == 0u)
        return dst;
    if (sa == 255u)
        return src;
    const uint32_t da = (dst >> 24) & 255u;
    const uint32_t inv = 255u - sa;
    const auto blend = [&] (int shift) -> uint32_t {
        const uint32_t s = (src >> shift) & 255u;
        const uint32_t d = (dst >> shift) & 255u;
        return std::min<uint32_t> (255u, s + (d * inv + 127u) / 255u);
    };
    const uint32_t outA = std::min<uint32_t> (255u, sa + (da * inv + 127u) / 255u);
    return (outA << 24) | (blend (16) << 16) | (blend (8) << 8) | blend (0);
}

bool writePngRgba (const std::string& path, int W, int H, const uint32_t* px)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f)
        return false;
    std::vector<unsigned char> raw;
    raw.reserve (static_cast<size_t> (H) * (static_cast<size_t> (W) * 4 + 1));
    for (int y = 0; y < H; ++y)
    {
        raw.push_back (0); // filter: none
        for (int x = 0; x < W; ++x)
        {
            const uint32_t p = px[y * W + x];
            const uint32_t alpha = (p >> 24) & 255u;
            const auto straight = [=] (int shift) {
                return static_cast<unsigned char> (alpha == 0 ? 0u
                    : std::min (255u, (((p >> shift) & 255u) * 255u + alpha / 2u) / alpha));
            };
            raw.push_back (straight (16));
            raw.push_back (straight (8));
            raw.push_back (straight (0));
            raw.push_back (static_cast<unsigned char> (alpha));
        }
    }
    std::vector<unsigned char> z { 0x78, 0x01 };
    uint32_t a = 1, b = 0;
    for (unsigned char c : raw)
    {
        a = (a + c) % 65521u;
        b = (b + a) % 65521u;
    }
    for (size_t pos = 0; pos < raw.size();)
    {
        const size_t n = std::min<size_t> (65535, raw.size() - pos);
        z.push_back (pos + n >= raw.size() ? 1 : 0);
        z.push_back (static_cast<unsigned char> (n & 255));
        z.push_back (static_cast<unsigned char> (n >> 8));
        z.push_back (static_cast<unsigned char> (~n & 255));
        z.push_back (static_cast<unsigned char> ((~n >> 8) & 255));
        z.insert (z.end(), raw.begin() + static_cast<long> (pos), raw.begin() + static_cast<long> (pos + n));
        pos += n;
    }
    uit::put32 (z, (b << 16) | a);
    const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
    std::fwrite (sig, 1, 8, f);
    std::vector<unsigned char> ihdr;
    uit::put32 (ihdr, static_cast<uint32_t> (W));
    uit::put32 (ihdr, static_cast<uint32_t> (H));
    ihdr.push_back (8); // bit depth 8
    ihdr.push_back (6); // color type 6: RGBA
    ihdr.push_back (0);
    ihdr.push_back (0);
    ihdr.push_back (0);
    uit::chunk (f, "IHDR", ihdr);
    uit::chunk (f, "IDAT", z);
    std::vector<unsigned char> empty;
    uit::chunk (f, "IEND", empty);
    std::fclose (f);
    return true;
}

std::string formatParamText (const FilterFixtureInstance& inst, int paramIdx, float plain)
{
    if (paramIdx < 0 || static_cast<size_t> (paramIdx) >= synthyboy::paramCount())
        return "";
    const auto& pi = synthyboy::paramTable()[paramIdx];
    if (std::strcmp (pi.key, "filter1.style") == 0 || std::strcmp (pi.key, "filter2.style") == 0)
    {
        const int model = static_cast<int> (std::lround (inst.param (FilterFixtureInstance::idx (
            std::strcmp (pi.key, "filter1.style") == 0 ? "filter1.model" : "filter2.model"))));
        const int count = synthyboy::filters::filterModelStyleCount (model);
        const auto* names = ui::shownFilterStyleNames (model, synthyboy::filters::filterModelStyleNames (model));
        return count > 0 && names != nullptr ? names[std::clamp (static_cast<int> (std::lround (plain)), 0, count - 1)] : "";
    }
    if (pi.numChoices > 0 && pi.choices != nullptr)
    {
        const int cidx = std::clamp (static_cast<int> (std::lround (plain - pi.min)), 0, pi.numChoices - 1);
        return synthyboy_mpc::ui::shownChoice (pi.key, cidx, pi.choices[cidx]);
    }
    char buf[64] {};
    synthyboy_mpc::overlay::uibind::formatValue (pi, plain, buf, sizeof buf);
    return buf;
}

std::string jsonEscape (const std::string& s)
{
    std::string out;
    for (char c : s)
    {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

} // namespace

int main (int argc, char** argv)
{
    std::string stateName = "init_f1";
    std::string outPng = "filter_overlay.png";
    std::string outJson = "";
    int knobs = 16;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--state" && i + 1 < argc)
            stateName = argv[++i];
        else if ((arg == "--dump-png" || arg == "--out" || arg == "-o") && i + 1 < argc)
            outPng = argv[++i];
        else if (arg == "--dump-json" && i + 1 < argc)
            outJson = argv[++i];
        else if (arg == "--knobs" && i + 1 < argc)
            knobs = std::atoi (argv[++i]);
        else if (arg == "--help" || arg == "-h")
        {
            std::printf ("Usage: %s [--state <init_f1|init_f2|f2_velo|f1_off_f2_on|both_off|formant|comb|expanded_filter|expanded_curve|dual_parallel>] [--dump-png <path>] [--dump-json <path>]\n", argv[0]);
            return 0;
        }
    }

    synthyboy_mpc::ui::QGroup::setWideDevice (knobs == 16);

    FilterFixtureInstance inst;
    const int nativeBase = static_cast<int> (synthyboy::paramCount()) + 82;
    const int stateParam = nativeBase + nativeui::kFilterBase;
    const int baseParam = nativeBase + nativeui::kFilterValueBase;
    const int actionParam = baseParam + nativeui::FilterAction;
    const int dispBase = nativeBase + nativeui::kFilterIndicatorBase;
    const int domainBase = nativeBase + nativeui::kFxDomainBase;

    int activeView = 0; // 0 = F1
    bool dualView = false;

    auto initDefaults = [&] (int f)
    {
        const std::string p = f == 2 ? "filter2." : "filter1.";
        inst.set (p + "on", 1.0f);
        inst.set (p + "model", 3.0f);
        inst.set (p + "style", 0.0f);
        inst.set (p + "cutoff", 12000.0f);
        inst.set (p + "resonance", 20.0f);
        inst.set (p + "drive", 0.0f);
        inst.set (p + "mix", 100.0f);
        inst.set (p + "keytrack", 0.0f);
        inst.set (p + "blend", 0.0f);
        inst.set (p + "clean_mode", 0.0f);
        inst.set (p + "env1", 1.0f);
    };
    initDefaults (1);
    initDefaults (2);
    inst.set ("filter.routing", 0.0f);

    // Apply specific fixture states
    if (stateName == "init_f1")
    {
        activeView = 0;
        inst.currentView = tabview::encodeFilterSplit (0, 0, tabview::kFilterSplit);
    }
    else if (stateName == "init_f2")
    {
        activeView = 1;
        inst.currentView = tabview::encodeFilterSplit (1, 0, tabview::kFilterSplit);
    }
    else if (stateName == "f2_velo")
    {
        activeView = 1;
        inst.currentView = tabview::encodeFilterSplit (1, 1, tabview::kFilterSplit);
    }
    else if (stateName == "f1_off_f2_on")
    {
        activeView = 0;
        inst.currentView = tabview::encodeFilterSplit (0, 0, tabview::kFilterSplit);
        inst.set ("filter1.on", 0.0f);
        inst.set ("filter2.on", 1.0f);
    }
    else if (stateName == "both_off")
    {
        activeView = 0;
        inst.currentView = tabview::encodeFilterSplit (0, 0, tabview::kFilterSplit);
        inst.set ("filter1.on", 0.0f);
        inst.set ("filter2.on", 0.0f);
    }
    else if (stateName == "formant" || stateName == "formant_off")
    {
        activeView = 0;
        inst.currentView = tabview::encodeFilterSplit (0, 0, tabview::kFilterSplit);
        inst.set ("filter1.on", 1.0f);
        inst.set ("filter1.on", stateName == "formant_off" ? 0.0f : 1.0f);
        inst.set ("filter1.model", 5.0f); // Formant
        inst.set ("filter1.formant.x", 45.0f);
        inst.set ("filter1.formant.y", 75.0f);
        inst.set ("filter1.formant.resonance", 60.0f);
        inst.set ("filter1.formant.spread", 30.0f);
    }
    else if (stateName == "comb" || stateName == "comb_off")
    {
        activeView = 0;
        inst.currentView = tabview::encodeFilterSplit (0, 0, tabview::kFilterSplit);
        inst.set ("filter1.on", 1.0f);
        inst.set ("filter1.on", stateName == "comb_off" ? 0.0f : 1.0f);
        inst.set ("filter1.model", 6.0f); // Comb
        inst.set ("filter1.comb_offset", 35.0f);
    }
    else if (stateName == "expanded_filter")
    {
        activeView = 0;
        inst.currentView = tabview::encodeFilterSplit (0, 0, tabview::kFilterExpandedFilter);
    }
    else if (stateName == "expanded_curve")
    {
        activeView = 2;
        inst.currentView = tabview::encodeFilterSplit (0, 0, tabview::kFilterExpandedCurve);
    }
    else if (stateName == "dual_parallel")
    {
        activeView = 0;
        inst.currentView = tabview::encodeFilterSplit (0, 0, tabview::kFilterExpandedFilter);
        dualView = true;
        inst.set ("filter1.on", 1.0f);
        inst.set ("filter2.on", 1.0f);
        inst.set ("filter.routing", 0.0f);
        inst.set ("filter2.cutoff", 2200.0f);
    }
    else
    {
        std::fprintf (stderr, "filter_render_fixture: unknown --state '%s'\n", stateName.c_str());
        return 2;
    }

    PageView<FilterUi> page ("filter1", 1, pages::filterTargets());
    page.ui().setHostBackground (true);

    UiStack stack;
    stack.openFull (FilterUi::kElementFilter);

    // Initial update to establish view
    int idx1[2], idx2[2];
    synthyboy::FilterResponse::readoutIndices (1, idx1);
    synthyboy::FilterResponse::readoutIndices (2, idx2);
    float liveTargets[4] = {
        inst.param (idx1[0]), inst.param (idx1[1]),
        inst.param (idx2[0]), inst.param (idx2[1])
    };

    ViewInput in;
    in.io = &inst;
    in.ui = &stack;
    in.nowMs = 1000.0;
    in.live = &inst.liveVal;
    in.panelEdgeX = page::kW;
    in.targets = liveTargets;
    page.update (in);

    if (dualView)
    {
        // Toggle dual view via native action
        const auto action = inst.nativeFilter.at (nativeui::FilterAction);
        inst.nativeFilter.report (action); // the host has consumed the current binding
        inst.nativeFilter.post (action, static_cast<float> (nativeui::FilterBoth) / nativeui::FilterLastAction);
        in.nowMs += 20.0;
        page.update (in);
        in.nowMs += 20.0;
        page.update (in); // pull response geometry after the queued native action
        if (! page.ui().isDual())
        {
            std::fprintf (stderr, "dual fixture failed to activate the acknowledged native F1+F2 action\n");
            return 3;
        }
    }

    const int fullW = 1280, fullH = 800;
    auto scene = std::make_unique<uit::Scene> (fullW, fullH, 0u);
    page.ui().draw (scene->builder());

    uit::Image im = uit::render (*scene);

    // Cover root markers
    {
        synthyboy_mpc::overlay::gpu::SceneData markerScene;
        synthyboy_mpc::overlay::gpu::SceneBuilder msb (markerScene);
        msb.begin (synthyboy_mpc::overlay::gpu::kKindUi, 0, fullW, fullH);
        msb.clear (0u);
        synthyboy_mpc::overlay::drawNativeMarkerCovers (msb, "filter1");
        std::vector<uint32_t> markerPixels;
        synthyboy_mpc::overlay::renderScene (markerScene, markerPixels);
        const size_t n = std::min (im.px.size(), markerPixels.size());
        for (size_t k = 0; k < n; ++k)
            im.px[k] = alphaOver (im.px[k], markerPixels[k]);
    }

    // Crop to 1280x628 (tab area y = 110..738)
    const int tabW = 1280, tabH = 628;
    std::vector<uint32_t> tabPixels (static_cast<size_t> (tabW) * static_cast<size_t> (tabH));
    for (int y = 0; y < tabH; ++y)
        for (int x = 0; x < tabW; ++x)
            tabPixels[static_cast<size_t> (y * tabW + x)] = im.at (x, y + 110);

    if (! writePngRgba (outPng, tabW, tabH, tabPixels.data()))
    {
        std::fprintf (stderr, "filter_render_fixture: error: failed to write %s\n", outPng.c_str());
        return 1;
    }

    std::printf ("filter_render_fixture: rendered %dx%d RGBA overlay for state '%s' to %s\n",
                 tabW, tabH, stateName.c_str(), outPng.c_str());

    if (! outJson.empty())
    {
        std::ofstream jf (outJson);
        if (jf.is_open())
        {
            jf << "{\n";
            jf << "  \"fixtureState\": \"" << stateName << "\",\n";
            jf << "  \"uiGeneration\": " << SYNTHYBOY_MPC_UI_GEN << ", \"qframeEnabled\": " << SYNTHYBOY_MPC_QFRAME_ENABLED << ",\n";
            jf << "  \"knobs\": " << knobs << ",\n";
            jf << "  \"view\": " << page.ui().view() << ",\n";
            jf << "  \"filterSelection\": " << page.ui().filterSelection() << ",\n";
            jf << "  \"scalarSelection\": " << page.ui().scalarSelection() << ",\n";
            jf << "  \"expandMode\": " << page.ui().expandMode() << ",\n";
            jf << "  \"split\": " << (page.ui().isSplit() ? "true" : "false") << ",\n";
            jf << "  \"dual\": " << (page.ui().isDual() ? "true" : "false") << ",\n";
            jf << "  \"stateParam\": " << stateParam << ",\n";
            jf << "  \"baseParam\": " << baseParam << ",\n";
            jf << "  \"actionParam\": " << actionParam << ",\n";
            jf << "  \"dispBase\": " << dispBase << ",\n";
            jf << "  \"domainBase\": " << domainBase << ",\n";
            jf << "  \"params\": {\n";
            bool firstP = true;
            for (size_t p = 0; p < synthyboy::paramCount(); ++p)
            {
                const synthyboy::ParamInfo& info = synthyboy::paramTable()[p];
                const float val = inst.param (static_cast<int> (p));
                const float norm = synthyboy::toNormalised (info, val);
                const std::string disp = formatParamText (inst, static_cast<int> (p), val);
                if (! firstP) jf << ",\n";
                firstP = false;
                jf << "    \"" << p << "\": {\"key\": \"" << info.key << "\", \"norm\": "
                   << std::fixed << std::setprecision (4) << norm << ", \"value\": \"" << jsonEscape (disp) << "\"}";
            }
            jf << "\n  },\n";
            jf << "  \"virtual\": {\n";
            bool firstV = true;
            const auto entry = [&] (int parameter, float norm, const std::string& caption, const std::string& display)
            {
                if (! firstV) jf << ",\n";
                firstV = false;
                jf << "    \"Parameter " << parameter << "\": {\"norm\": " << std::setprecision (6) << norm
                   << ", \"caption\": \"" << jsonEscape (caption) << "\", \"display\": \"" << jsonEscape (display) << "\"}";
            };

            // State handle (Parameter 2850, 5 states 0..4)
            static const char* const viewNames[] = { "OFF", "F1", "F2", "NOTE", "VELO" };
            const int pv = std::clamp (inst.publishedView, 0, nativeui::kFilterLastViewFrame);
            entry (stateParam, static_cast<float> (pv) / static_cast<float> (nativeui::kFilterLastViewFrame), "STATE", viewNames[pv]);

            // Base parameter handles (Parameter 2851 .. 2877)
            for (int slot = 0; slot < nativeui::FilterSlots; ++slot)
            {
                if (slot == nativeui::FilterAction)
                    continue;
                const auto cell = inst.nativeFilter.at (slot).cell();
                float norm = 0.0f;
                std::string caption = nativeui::kFilterNames[slot];
                std::string display = "";
                if (slot >= nativeui::FilterPoint && slot < nativeui::FilterLegato)
                {
                    const int fieldIdx = slot - nativeui::FilterPoint;
                    norm = inst.publishedValues[fieldIdx];
                    const float plain = inst.publishedValues[nativeui::kFilterCurveFields + fieldIdx];
                    char buf[32] {};
                    if (slot == nativeui::FilterCurve)
                        std::snprintf (buf, sizeof buf, "%.2f", static_cast<double> (plain));
                    else if (slot == nativeui::FilterSnap)
                        std::snprintf (buf, sizeof buf, "%s", plain >= 0.5f ? "ON" : "OFF");
                    else if (slot == nativeui::FilterY)
                        std::snprintf (buf, sizeof buf, "%d%%", static_cast<int> (std::lround (plain)));
                    else
                        std::snprintf (buf, sizeof buf, "%d", static_cast<int> (std::lround (plain)));
                    display = buf;
                }
                else if (cell.target >= 0 && static_cast<size_t> (cell.target) < synthyboy::paramCount())
                {
                    const auto& pi = synthyboy::paramTable()[cell.target];
                    const float val = inst.param (cell.target);
                    norm = synthyboy::toNormalised (pi, val);
                    display = formatParamText (inst, cell.target, val);
                }
                entry (baseParam + slot, norm, caption, display);
            }

            // Action handle (Parameter 2878)
            entry (actionParam, 0.0f, "ACTION", "");

            // Indicator handles (Parameter 2879 .. 2883)
            for (int ind = 0; ind < nativeui::kFilterIndicators; ++ind)
            {
                const float norm = static_cast<float> (inst.publishedIndicators[ind]) / 3.0f;
                entry (dispBase + ind, norm, "INDICATOR " + std::to_string (ind), std::to_string (inst.publishedIndicators[ind]));
            }

            // Choice domain: Model (Parameter 2779) and Style (Parameter 2800 + modelChoice)
            const int editF = page.ui().editFilter();
            const std::string fPrefix = editF == 2 ? "filter2." : "filter1.";
            const int modelChoice = static_cast<int> (std::lround (inst.param (FilterFixtureInstance::idx (fPrefix + "model"))));
            const auto* pModel = synthyboy::findParamByKey ((fPrefix + "model").c_str());
            const std::string modelStr = (pModel && modelChoice >= 0 && modelChoice < pModel->numChoices)
                ? pModel->choices[modelChoice] : "Digital";
            entry (domainBase + 0, static_cast<float> (modelChoice) / 26.0f, "MODEL", modelStr);

            const int styleChoice = static_cast<int> (std::lround (inst.param (FilterFixtureInstance::idx (fPrefix + "style"))));
            const int styleCount = synthyboy::filters::filterModelStyleCount (modelChoice);
            const auto* styleNames = ui::shownFilterStyleNames (modelChoice, synthyboy::filters::filterModelStyleNames (modelChoice));
            const std::string styleStr = styleNames != nullptr && styleCount > 0
                ? styleNames[std::clamp (styleChoice, 0, styleCount - 1)] : "";
            entry (domainBase + 21 + modelChoice, styleCount > 1 ? static_cast<float> (styleChoice) / static_cast<float> (styleCount - 1) : 0.0f,
                   "STYLE", styleStr);

            jf << "\n  }\n";
            jf << "}\n";
        }
    }

    return 0;
}
