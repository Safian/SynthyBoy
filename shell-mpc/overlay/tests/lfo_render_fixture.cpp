// SynthyBoy - C++ LFO overlay render fixture
// Renders the real LfoUi + scene_cpu.h overlay and dumps matching host-native parameter/handle state
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

#include "shell-mpc/native_ui_params.h"
#include "shell-mpc/overlay/lfo_ui.h"
#include "shell-mpc/overlay/native_marker_covers.h"
#include "shell-mpc/overlay/play_view.h"
#include "shell-mpc/overlay/scene_cpu.h"
#include "shell-mpc/overlay/tests/fakes.h"
#include "shell-mpc/ui/tests/ui_test.h"
#include "synthyboy/user_content.h"

using namespace synthyboy_mpc;
using namespace synthyboy_mpc::overlay;

namespace {

class LfoFixtureInstance final : public IInstanceView
{
public:
    LfoFixtureInstance()
    {
        const synthyboy::ParamInfo* t = synthyboy::paramTable();
        for (size_t i = 0; i < synthyboy::paramCount(); ++i)
            v_[i] = t[i].def;
        contentPtr = std::make_shared<const synthyboy::StateContent> (synthyboy::StateContent {});
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
    void setPlaySelection (int g, int i) noexcept override { sel[std::clamp (g, 0, 2)] = i; }
    int playLastSelector() const noexcept override { return 2; }
    int tabView (int what) const noexcept override { return what >= 0 && what < 3 ? tv[what] : 0; }
    void setTabView (int what, int value) noexcept override { if (what >= 0 && what < 3) tv[what] = value; }
    void setUiFullOpen (bool o) noexcept override { fullOpen = o; }

    bool nativeLfoControls() const noexcept override { return nativeEnabled; }
    void setNativeLfoState (const nativeui::lfo::ViewState& state) noexcept override
    {
        native = state;
        nativeBank.publish (state);
        nativeBank.report (nativeBank.publication());
    }
    bool takeNativeLfo (const nativeui::lfo::ViewState& state, int field, float& value) noexcept override
    {
        return nativeBank.take (state, field, value);
    }
    bool nativeLfoShapeEditor() const noexcept override { return nativeEditorEnabled; }
    void setNativeLfoEditor (const nativeui::lfoeditor::State& state) noexcept override
    {
        nativeEditor = state;
        nativeEditorBank.publish (state);
        for (int field = 0; field < nativeui::lfoeditor::kCount; ++field)
            nativeEditorBank.report (nativeEditorBank.at (field));
    }
    bool takeNativeLfoEditor (const nativeui::lfoeditor::State& state, int field, float& value) noexcept override
    {
        return nativeEditorBank.take (state, field, value);
    }
    bool nativeLfoShapeDetails() const noexcept override { return nativeDetailEnabled; }
    void setNativeLfoDetail (const nativeui::lfodetail::State& state) noexcept override
    {
        nativeDetail.publish (state);
        for (int field = 0; field < nativeui::lfodetail::kCount; ++field)
            nativeDetail.report (nativeDetail.at (field));
    }
    bool takeNativeLfoDetail (const nativeui::lfodetail::State& state, int field, float& value) noexcept override
    {
        return nativeDetail.take (state, field, value);
    }

    std::shared_ptr<const synthyboy::StateContent> content() const override { return contentPtr; }
    void setLfoShape (int lfo, const synthyboy::ContentLfoShape& s) override
    {
        synthyboy::StateContent next = *contentPtr;
        bool done = false;
        for (synthyboy::ContentLfoShape& e : next.lfoShapes)
            if (e.lfo == lfo) { e = s; done = true; }
        if (! done)
            next.lfoShapes.push_back (s);
        contentPtr = std::make_shared<const synthyboy::StateContent> (next);
    }

    static int idx (const std::string& key)
    {
        const auto* p = synthyboy::findParamByKey (key.c_str());
        return p ? static_cast<int> (p - synthyboy::paramTable()) : -1;
    }
    void set (const std::string& key, float v)
    {
        const int i = idx (key);
        if (i >= 0)
            v_[i] = v;
    }
    float get (const std::string& key) const
    {
        const int i = idx (key);
        return i >= 0 ? v_[i] : 0.0f;
    }

    bool nativeEnabled = true;
    bool nativeEditorEnabled = true;
    bool nativeDetailEnabled = true;
    nativeui::lfo::ViewState native;
    nativeui::lfo::Bank nativeBank;
    nativeui::lfoeditor::State nativeEditor;
    nativeui::lfoeditor::Bank nativeEditorBank;
    nativeui::lfodetail::Bank nativeDetail;

    synthyboy::LiveValues liveVal {};
    bool haveLive = true;
    int tv[3] = { 0, 0, 0 };
    int sel[3] = { 2, 1, 1 };
    int q[16] = { -1 };
    bool fullOpen = false;
    std::shared_ptr<const synthyboy::StateContent> contentPtr;
    float v_[synthyboy::kMaxParams] = {};
    uint64_t generation = 1;
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
            // scene_cpu stores premultiplied ARGB; PNG/Pillow expect straight RGBA.
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

std::string formatParamText (int paramIdx, float plain)
{
    if (paramIdx < 0 || static_cast<size_t> (paramIdx) >= synthyboy::paramCount())
        return "";
    const auto& pi = synthyboy::paramTable()[paramIdx];
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

inline TouchEvent ev (TouchEvent::Kind k, int x, int y) { TouchEvent e; e.kind = k; e.x = x; e.y = y; return e; }

} // namespace

int main (int argc, char** argv)
{
    std::string stateName = "standard_hz";
    std::string outPng = "lfo_overlay.png";
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
            std::printf ("Usage: %s [--state <standard_hz|standard_smooth_off|standard_bpm|standard_key|drawn_expanded|rnd_hz>] [--dump-png <path>] [--dump-json <path>]\n", argv[0]);
            return 0;
        }
    }

    synthyboy_mpc::ui::QGroup::setWideDevice (knobs == 16);

    LfoFixtureInstance inst;
    const int nativeBase = static_cast<int> (synthyboy::paramCount()) + 82;
    const int baseParam = nativeBase + nativeui::kLfoBase;

    // Default configuration for LFO 1
    inst.set ("lfo1.rate", 1.0f);
    inst.set ("lfo1.sync", 0.0f);
    inst.set ("lfo1.shape", 0.0f); // Sine
    inst.set ("lfo1.smooth", 1.0f);
    inst.set ("lfo1.smooth_time", 150.0f);
    inst.set ("lfo1.fade_in", 0.0f);
    inst.set ("lfo1.phase", 0.0f);
    inst.set ("lfo1.delay", 0.0f);
    inst.set ("lfo1.swing", 50.0f);
    inst.set ("lfo1.stereo", 0.0f);

    int activeLayout = 1; // 1 = standard Hz
    int selection = 1;    // 1 = LFO 1
    bool isEditorOpen = false;

    if (stateName == "standard_smooth_off")
    {
        activeLayout = 1;
        inst.set ("lfo1.smooth", 0.0f);
        inst.set ("lfo1.smooth_time", 150.0f);
    }
    else if (stateName == "standard_bpm")
    {
        activeLayout = 2; // BPM sync
        inst.set ("lfo1.sync", 1.0f);
        inst.set ("lfo1.shape", 1.0f); // Triangle
        inst.set ("lfo1.division", 8.0f); // 1/4 note
    }
    else if (stateName == "standard_key")
    {
        activeLayout = 3; // KEY sync
        inst.set ("lfo1.sync", 4.0f);
        inst.set ("lfo1.shape", 2.0f); // Saw Up
        inst.set ("lfo1.keytrack_transpose", 0.0f);
        inst.set ("lfo1.keytrack_tune", 0.0f);
    }
    else if (stateName == "drawn_expanded")
    {
        activeLayout = 1;
        inst.set ("lfo1.shape", 12.0f); // Custom Drawn shape
        synthyboy::ContentLfoShape cs;
        cs.lfo = 1;
        cs.points = { { 0.0f, 0.0f, 0.0f }, { 0.25f, 0.85f, 0.0f }, { 0.5f, -0.35f, 0.0f }, { 0.75f, 0.65f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
        inst.setLfoShape (1, cs);
        isEditorOpen = true;
    }
    else if (stateName == "rnd_hz" || stateName == "rnd_bpm" || stateName == "rnd_key")
    {
        activeLayout = 4; // RND Hz
        selection = 11;   // RND 1
        inst.setPlaySelection (2, 11);
        inst.tv[2] = 1; // RND 1
        inst.set ("rlfo1.rate", 4.0f);
        inst.set ("rlfo1.sync", stateName == "rnd_key" ? 4.0f : stateName == "rnd_bpm" ? 1.0f : 0.0f);
        activeLayout = stateName == "rnd_key" ? 6 : stateName == "rnd_bpm" ? 5 : 4;
        inst.set ("rlfo1.style", 1.0f); // Sample & Hold
    }

    // Authentic live values for cursor and curve rendering
    inst.liveVal.numLfoVoices = 1;
    inst.liveVal.lfoVoicePhase[0][0] = 0.42f;
    inst.liveVal.source[uibind::lfoSource (1) - 1] = 0.707f;

    PageView<LfoUi> page ("lfo1", 1);
    UiStack stack;
    stack.openFull (page.pageElement());

    ViewInput in;
    in.io = &inst;
    in.ui = &stack;
    in.nowMs = 1000.0;
    in.live = &inst.liveVal;
    in.panelEdgeX = page::kW;
    page.update (in);

    if (selection >= 11)
    {
        inst.nativeBank.post (nativeui::lfo::kSelection, static_cast<float> (selection - 1) / (nativeui::lfo::kInstances - 1));
        page.update (in);
    }

    if (isEditorOpen)
    {
        inst.nativeBank.post (nativeui::lfo::kAction, 1.0f); // actual host EXPAND action
        page.update (in);
        if (inst.nativeEditor.context.owner == 0)
        {
            std::fprintf (stderr, "Drawn fixture failed to open the real native shape editor\n");
            return 1;
        }
    }

    page.ui().setHostBackground (true);

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
        synthyboy_mpc::overlay::drawNativeMarkerCovers (msb, "lfo1");
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
        std::fprintf (stderr, "lfo_render_fixture: error: failed to write %s\n", outPng.c_str());
        return 1;
    }

    std::printf ("lfo_render_fixture: rendered %dx%d RGBA overlay for state '%s' to %s\n",
                 tabW, tabH, stateName.c_str(), outPng.c_str());

    if (! outJson.empty())
    {
        std::ofstream jf (outJson);
        if (jf.is_open())
        {
            jf << "{\n";
            jf << "  \"fixtureState\": \"" << stateName << "\",\n";
            jf << "  \"uiGeneration\": " << SYNTHYBOY_MPC_UI_GEN << ", \"knobs\": " << knobs << ",\n";
            jf << "  \"layout\": " << activeLayout << ",\n";
            jf << "  \"selection\": " << selection << ",\n";
            jf << "  \"base\": " << baseParam << ",\n";
            jf << "  \"liveVoicePhase\": " << inst.liveVal.lfoVoicePhase[0][0] << ",\n";
            jf << "  \"params\": {\n";
            bool firstP = true;
            for (size_t p = 0; p < synthyboy::paramCount(); ++p)
            {
                const synthyboy::ParamInfo& info = synthyboy::paramTable()[p];
                const float val = inst.param (static_cast<int> (p));
                const float norm = synthyboy::toNormalised (info, val);
                const std::string disp = formatParamText (static_cast<int> (p), val);
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
            entry (baseParam + nativeui::lfo::kLayout, static_cast<float> (inst.native.layout) / 6.0f, "LAYOUT", "");
            const std::string prefix = selection <= 10 ? "lfo" + std::to_string (selection) + "." : "rlfo" + std::to_string (selection - 10) + ".";
            for (int f = 0; f < nativeui::lfo::kCount; ++f)
            {
                if (f == nativeui::lfo::kLayout) continue;
                const char* suffix = nativeui::lfo::kSuffixes[f];
                const int target = suffix != nullptr ? LfoFixtureInstance::idx (prefix + suffix) : -1;
                const std::string display = f == nativeui::lfo::kSelection
                    ? (selection <= 10 ? "LFO " + std::to_string (selection) : "RND " + std::to_string (selection-10))
                    : target >= 0 ? formatParamText (target, inst.param (target)) : "";
                entry (baseParam + f, inst.native.values[f], nativeui::lfo::kNames[f], display);
            }
            namespace ed = nativeui::lfoeditor;
            entry (nativeBase + nativeui::kLfoEditor + ed::kView, inst.nativeEditor.context.owner != 0 ? 1.0f : 0.0f, "VIEW", "");
            for (int f = 0; f < ed::kCount; ++f)
            {
                const auto range = ed::rangeOf (f, inst.nativeEditor.context.points, inst.nativeEditor.scale());
                const double value = range.decode (inst.nativeEditor.values[f]);
                char display[64] {};
                if (ed::kFields[f].type == ed::kToggle)
                    std::snprintf (display, sizeof display, "%s", value > 0.5 ? "On" : "Off");
                else if (f == ed::kMode)
                {
                    const auto formatted = formatParamText (LfoFixtureInstance::idx ("lfo1.sync_type"), static_cast<float> (value));
                    std::snprintf (display, sizeof display, "%s", formatted.c_str());
                }
                else if (f == ed::kPoint && inst.nativeEditor.context.selection.count() == 0)
                    std::snprintf (display, sizeof display, "-");
                else if (range.units == 100)
                    std::snprintf (display, sizeof display, "%.2f", value);
                else if (ed::kFields[f].type == ed::kNumber)
                    std::snprintf (display, sizeof display, f == ed::kCurve || f == ed::kBend ? "%+.0f" : "%.0f", value);
                entry (nativeBase + nativeui::kLfoEditor + ed::kValues + f, inst.nativeEditor.values[f], ed::kFields[f].name, display);
            }
            for (int i = 0; i < ed::kAvailabilityCount; ++i)
                entry (nativeBase + nativeui::kLfoEditor + ed::kAvailability + i,
                       inst.nativeEditor.allows (ed::kOptional[i]) ? 1.0f : 0.0f, "AVAILABLE", "");
            entry (nativeBase + nativeui::kLfoEditor + ed::kScale, inst.nativeEditor.scale() ? 1.0f : 0.0f, "SCALE", "");
            entry (nativeBase + nativeui::kLfoDetail, 0.0f, "DETAIL", "");
            jf << "\n";
            jf << "  }\n";
            jf << "}\n";
        }
    }

    return 0;
}
