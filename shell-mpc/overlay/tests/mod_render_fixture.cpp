// SynthyBoy - C++ Mod matrix overlay render fixture
// Renders the real ModUi + scene_cpu.h overlay and dumps matching host-native parameter/handle state
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
#include "shell-mpc/overlay/mod_ui.h"
#include "shell-mpc/overlay/native_marker_covers.h"
#include "shell-mpc/overlay/scene_cpu.h"
#include "shell-mpc/overlay/tests/fakes.h"
#include "shell-mpc/ui/tests/ui_test.h"
#include "synthyboy/modulation.h"
#include "synthyboy/params.h"
#include "synthyboy/user_content.h"

using namespace synthyboy_mpc;
using namespace synthyboy_mpc::overlay;

namespace {

class ModFixtureInstance final : public IInstanceView
{
public:
    ModFixtureInstance()
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
    void setPlaySelection (int g, int i) noexcept override { sel[std::clamp (g, 0, 2)] = i; }
    int playLastSelector() const noexcept override { return 2; }
    int tabView (int what) const noexcept override { return what >= 0 && what < 3 ? tv[what] : 0; }
    void setTabView (int what, int value) noexcept override { if (what >= 0 && what < 3) tv[what] = value; }
    void setUiFullOpen (bool o) noexcept override { fullOpen = o; }

    void setQGroup (const int* t, int n) noexcept override
    {
        for (int i = 0; i < 16; ++i) q[i] = i < n ? t[i] : -1;
        qN = n;
    }

    void writeParam (int i, float norm) noexcept override
    {
        if (i >= 0 && static_cast<size_t> (i) < synthyboy::paramCount())
        {
            const synthyboy::ParamInfo& p = synthyboy::paramTable()[i];
            v_[i] = synthyboy::constrainValue (p, synthyboy::fromNormalised (p, norm));
        }
    }

    void setQVirtual (int id, float v) noexcept override
    {
        if (id >= 0 && id < 10) virtualOut[id] = v;
    }
    bool takeQVirtual (int id, float& v) noexcept override
    {
        if (id < 0 || id >= 10 || ! virtualDue[id])
            return false;
        virtualDue[id] = false;
        v = virtualIn[id];
        return true;
    }

    void setNativeModMatrix (int view, const nativeui::FxCell* fields, const int* rows) noexcept override
    {
        nativeView = view;
        for (int r = 0; r < nativeui::mod::kRows; ++r)
            nativeRows[r] = rows != nullptr ? rows[r] : 0;
        for (int f = 0; f < nativeui::mod::kFields; ++f)
            nativeCells[f] = fields != nullptr ? fields[f] : nativeui::FxCell {};
    }

    void setNativeModNavigation (int mode, int source, int destination, const nativeui::FxCell* fields) noexcept override
    {
        nativeMode = mode;
        nativeSourceGroup = source;
        nativeDestinationGroup = destination;
        for (int f = 0; f < nativeui::mod::kNavigation; ++f)
            nativeNavigation[f] = fields != nullptr ? fields[f] : nativeui::FxCell {};
    }

    std::shared_ptr<const synthyboy::StateContent> content() const override { return contentPtr; }

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

    int nativeView = 0;
    int nativeRows[nativeui::mod::kRows] = {};
    nativeui::FxCell nativeCells[nativeui::mod::kFields] = {};
    int nativeMode = 0;
    int nativeSourceGroup = 0;
    int nativeDestinationGroup = 0;
    nativeui::FxCell nativeNavigation[nativeui::mod::kNavigation] = {};

    float virtualOut[10] = {};
    float virtualIn[10] = {};
    bool virtualDue[10] = {};
    synthyboy::LiveValues liveVal {};
    bool haveLive = false;
    int tv[3] = { 0, 0, 0 };
    int sel[3] = { 2, 1, 1 };
    int q[16] = { -1 };
    int qN = 0;
    bool fullOpen = false;
    std::shared_ptr<const synthyboy::StateContent> contentPtr;
    float v_[synthyboy::kMaxParams] = {};
    uint64_t generation = 1;
};

uint32_t alphaOver (uint32_t dst, uint32_t src)
{
    const uint32_t sa = (src >> 24) & 255u;
    if (sa == 0) return dst;
    if (sa == 255u) return src;
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
    ihdr.push_back (0); // compression
    ihdr.push_back (0); // filter
    ihdr.push_back (0); // interlace
    uit::chunk (f, "IHDR", ihdr);
    uit::chunk (f, "IDAT", z);
    std::vector<unsigned char> empty;
    uit::chunk (f, "IEND", empty);
    std::fclose (f);
    return true;
}

} // namespace

int main (int argc, char** argv)
{
    std::string stateName = "populated";
    std::string outPng;
    std::string outJson;
    int knobs = 16;
    int gen = 2;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--state" && i + 1 < argc)
            stateName = argv[++i];
        else if (arg == "--out-overlay" && i + 1 < argc)
            outPng = argv[++i];
        else if (arg == "--out-json" && i + 1 < argc)
            outJson = argv[++i];
        else if (arg == "--knobs" && i + 1 < argc)
            knobs = std::atoi (argv[++i]);
        else if (arg == "--gen" && i + 1 < argc)
            gen = std::atoi (argv[++i]);
    }

    if (outPng.empty())
    {
        std::fprintf (stderr, "mod_render_fixture: error: --out-overlay <path.png> is required\n");
        return 2;
    }

    synthyboy_mpc::ui::QGroup::setWideDevice (knobs == 16);

    ModFixtureInstance inst;
    int totalActive = 0;

    if (stateName == "populated" || stateName == "default")
    {
        // Equivalent historical before/after five rows:
        // 1) LFO1 -> Filter1Cutoff +65% BI ON
        // 2) ENV1 -> Osc1Tune -30% BI ON
        // 3) Macro1 -> Osc1Frame +85% UNI ON
        // 4) ModWheel -> LFO1Rate +50% UNI ON
        // 5) Velocity -> MasterVolume +40% UNI ON
        inst.mod (1, 7, "filter1.cutoff", 65.0f, true, false);
        inst.mod (2, 1, "osc1.tune", -30.0f, true, false);
        inst.mod (3, 19, "osc1.frame", 85.0f, false, false);
        inst.mod (4, 27, "lfo1.rate", 50.0f, false, false);
        inst.mod (5, 23, "master.volume", 40.0f, false, false);
        totalActive = 5;
    }
    else if (stateName == "detail" || stateName == "detail_beats" || stateName == "detail_confirm")
    {
        inst.mod (1, 7, "filter1.cutoff", 65.0f, true, false);
        inst.mod (2, 1, "osc1.tune", -30.0f, true, false);
        inst.mod (3, 19, "osc1.frame", 85.0f, false, false);
        inst.mod (4, 27, "lfo1.rate", 50.0f, false, false);
        inst.mod (5, 23, "master.volume", 40.0f, false, false);
        inst.mod (6, 28, "filter1.reso", 25.0f, false, false);
        inst.mod (7, 2, "osc2.tune", 10.0f, true, false);
        inst.mod (8, 8, "filter2.cutoff", -45.0f, true, false);
        // Non-zero detail slot: slot 9 (index 8, mod09)
        inst.mod (9, 8, "filter1.cutoff", 65.0f, true, false);
        inst.set ("mod09.aux_source", 0.0f);     // NONE
        inst.set ("mod09.aux_output", 75.0f);
        inst.set ("mod09.aux_inv", 0.0f);
        inst.set ("mod09.aux_power", 0.0f);
        inst.set ("mod09.power", 0.0f);
        inst.set ("mod09.stereo", 1.0f);
        inst.set ("mod09.rise", 0.2f);
        inst.set ("mod09.fall", 0.4f);
        inst.set ("mod09.delay", 0.3f);
        inst.set ("mod09.delay_sync", stateName == "detail_beats" ? 1.0f : 0.0f);
        inst.set ("mod09.delay_beats", 2.0f);
        totalActive = 9;
    }
    else if (stateName == "empty")
    {
        totalActive = 0;
    }
    else
    {
        std::fprintf (stderr, "mod_render_fixture: unknown state '%s'\n", stateName.c_str());
        return 1;
    }

    ModUi ui;
    UiStack stack;
    stack.openFull (ModUi::kElementPage);

    if (stateName == "detail" || stateName == "detail_beats" || stateName == "detail_confirm")
    {
        ui.matrix().scrollByRows (3);
        ui.openSlotDetail (8, stack); // Slot 9 (index 8)
    }

    page::Input in;
    in.instance = 1;
    in.live = inst.haveLive ? &inst.liveVal : nullptr;

    ui.setNativeControls (true);
    ui.update (inst, stack, 1000.0, in);
    if (stateName == "detail_confirm")
    {
        ui.detail().confirmDelete();
        ui.update (inst, stack, 1000.0, in);
    }
    ui.setHostBackground (true);

    const int fullW = 1280, fullH = 800;
    auto scene = std::make_unique<uit::Scene> (fullW, fullH, 0u);
    ui.draw (scene->builder());

    uit::Image im = uit::render (*scene);

    // Cover root markers for custom overlay states (e.g. popups).
    // In normal populated matrix, board, targets, and detail view, the host TUI is entirely native without a GPU plane.
    if (stateName != "populated" && stateName != "detail" && stateName != "detail_beats" && stateName != "detail_confirm" && stateName != "empty_board" && stateName != "targets")
    {
        synthyboy_mpc::overlay::gpu::SceneData markerScene;
        synthyboy_mpc::overlay::gpu::SceneBuilder msb (markerScene);
        msb.begin (synthyboy_mpc::overlay::gpu::kKindUi, 0, fullW, fullH);
        msb.clear (0u);
        synthyboy_mpc::overlay::drawNativeMarkerCovers (msb, "mod", 0x0b1025u, 0x0b1025u);
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
        std::fprintf (stderr, "mod_render_fixture: error: failed to write %s\n", outPng.c_str());
        return 1;
    }

    std::printf ("mod_render_fixture: rendered %dx%d RGBA overlay for state '%s' to %s\n",
                 tabW, tabH, stateName.c_str(), outPng.c_str());

    if (! outJson.empty())
    {
        const int pCount = static_cast<int> (synthyboy::paramCount());
        const int base = pCount + 82; // mixer_peek_index: param_count + 82 (LEGACY_EXTRAS=70 + 12)
        const int viewParam = base + 169; // 2888
        const int rowsBase = viewParam + 1; // 2889
        const int fieldsBase = rowsBase + 7; // 2896
        const int navBase = base + 225; // 2944

        std::ofstream jf (outJson);
        if (jf.is_open())
        {
            jf << "{\n";
            jf << "  \"fixtureState\": \"" << stateName << "\",\n";
            jf << "  \"uiGeneration\": " << gen << ", \"knobs\": " << knobs << ",\n";
            jf << "  \"totalActive\": " << totalActive << ",\n";
            jf << "  \"nativeView\": " << inst.nativeView << ",\n";
            jf << "  \"nativeMode\": " << inst.nativeMode << ",\n";
            jf << "  \"nativeSourceGroup\": " << inst.nativeSourceGroup << ",\n";
            jf << "  \"nativeDestinationGroup\": " << inst.nativeDestinationGroup << ",\n";
            jf << "  \"nativeRows\": [";
            for (int r = 0; r < 7; ++r)
                jf << inst.nativeRows[r] << (r < 6 ? ", " : "");
            jf << "],\n";

            // Virtual remapped parameter state for skin sketch adapter
            jf << "  \"virtual\": {\n";
            // View parameter
            jf << "    \"Parameter " << viewParam << "\": {\"norm\": "
               << (static_cast<float> (inst.nativeView) / 4.0f)
               << ", \"caption\": \"Mod View\", \"display\": \"" << inst.nativeView << "\"},\n";

            // Row gates (Parameter 2889 .. 2895)
            for (int r = 0; r < 7; ++r)
            {
                const int slot = inst.nativeRows[r];
                const float norm = static_cast<float> (slot) / 64.0f;
                jf << "    \"Parameter " << (rowsBase + r) << "\": {\"norm\": " << norm
                   << ", \"caption\": \"Mod Row " << (r + 1) << " Slot\", \"display\": \"" << slot << "\"},\n";
            }

            // Navigation parameters
            jf << "    \"Parameter " << navBase << "\": {\"norm\": "
               << (static_cast<float> (inst.nativeMode) / 3.0f)
               << ", \"caption\": \"Mode\", \"display\": \"" << (inst.nativeMode == 1 ? "ALL" : inst.nativeMode == 2 ? "BY SOURCE" : "BY DEST") << "\"},\n";
            jf << "    \"Parameter " << (navBase + 1) << "\": {\"norm\": "
               << (static_cast<float> (inst.nativeSourceGroup) / 5.0f)
               << ", \"caption\": \"Source Group\", \"display\": \"ALL SOURCES\"},\n";
            jf << "    \"Parameter " << (navBase + 2) << "\": {\"norm\": "
               << (static_cast<float> (inst.nativeDestinationGroup) / 10.0f)
               << ", \"caption\": \"Target Group\", \"display\": \"ALL MODULES\"},\n";
            jf << "    \"Parameter " << (navBase + 3) << "\": {\"norm\": 0.0, \"caption\": \"+ ADD\", \"display\": \"+ ADD\"},\n";

            // Row fields: AMOUNT, POLARITY, ROUTE
            struct RowData {
                const char* src;
                const char* dst;
                float amt;
                bool bi;
                bool byp;
            };
            const RowData activeRows[5] = {
                { "LFO 1", "Filter 1 Cutoff", 65.0f, true, false },
                { "Env 1", "Osc 1 Tune", -30.0f, true, false },
                { "Macro 1", "Osc 1 Frame", 85.0f, false, false },
                { "Mod Wheel", "LFO 1 Rate", 50.0f, false, false },
                { "Velocity", "Master Volume", 40.0f, false, false }
            };

            for (int r = 0; r < 7; ++r)
            {
                const int slot = inst.nativeRows[r];
                const bool act = (slot > 0 && r < 5 && totalActive > 0);
                const float amt = act ? activeRows[r].amt : 0.0f;
                const float amtNorm = act ? (amt * 0.01f + 1.0f) * 0.5f : 0.5f;
                char amtDisp[32];
                std::snprintf (amtDisp, sizeof amtDisp, "%+.1f %%", static_cast<double> (amt));
                const bool bi = act ? activeRows[r].bi : false;
                const bool byp = act ? activeRows[r].byp : false;

                const int pAmt = fieldsBase + r * 3 + 0;
                const int pPol = fieldsBase + r * 3 + 1;
                const int pByp = fieldsBase + r * 3 + 2;

                jf << "    \"Parameter " << pAmt << "\": {\"norm\": " << amtNorm
                   << ", \"caption\": \"AMOUNT\", \"display\": \"" << amtDisp << "\"},\n";
                jf << "    \"Parameter " << pPol << "\": {\"norm\": " << (bi ? 1.0 : 0.0)
                   << ", \"caption\": \"POLARITY\", \"display\": \"" << (bi ? "BI" : "UNI") << "\"},\n";
                jf << "    \"Parameter " << pByp << "\": {\"norm\": " << (byp ? 1.0 : 0.0)
                   << ", \"caption\": \"ROUTE\", \"display\": \"" << (byp ? "OFF" : "ON") << "\"},\n";

                // Row navigation: SOURCE, TARGET, MORE
                const int pSrc = navBase + 4 + r * 3 + 0;
                const int pDst = navBase + 4 + r * 3 + 1;
                const int pMor = navBase + 4 + r * 3 + 2;

                const char* srcName = act ? activeRows[r].src : "---";
                const char* dstName = act ? activeRows[r].dst : "---";

                jf << "    \"Parameter " << pSrc << "\": {\"norm\": " << (act ? 0.5 : 0.0)
                   << ", \"caption\": \"SOURCE\", \"display\": \"" << srcName << "\"},\n";
                jf << "    \"Parameter " << pDst << "\": {\"norm\": " << (act ? 0.5 : 0.0)
                   << ", \"caption\": \"TARGET\", \"display\": \"" << dstName << "\"},\n";
                jf << "    \"Parameter " << pMor << "\": {\"norm\": 0.0, \"caption\": \"EDIT\", \"display\": \"EDIT\"},\n";
            }

            if (inst.nativeView == 3 || inst.nativeView == 4)
            {
                const int openSlotIndex = (stateName == "detail" || stateName == "detail_beats" || stateName == "detail_confirm") ? 8 : 0;
                const auto sp = synthyboy::modSlotParams (openSlotIndex);
                const float amt = inst.param (sp.amount);
                const float amtNorm = (amt * 0.01f + 1.0f) * 0.5f;
                char amtDisp[32];
                std::snprintf (amtDisp, sizeof amtDisp, "%+.1f %%", static_cast<double> (amt));
                const bool bi = inst.param (sp.bipolar) > 0.5f;
                const bool byp = inst.param (sp.bypass) > 0.5f;

                jf << "    \"Parameter " << (fieldsBase + 21) << "\": {\"norm\": " << amtNorm
                   << ", \"caption\": \"AMOUNT\", \"display\": \"" << amtDisp << "\"},\n";
                jf << "    \"Parameter " << (fieldsBase + 22) << "\": {\"norm\": " << (bi ? 1.0 : 0.0)
                   << ", \"caption\": \"POLARITY\", \"display\": \"" << (bi ? "BI" : "UNI") << "\"},\n";
                jf << "    \"Parameter " << (fieldsBase + 23) << "\": {\"norm\": " << (byp ? 1.0 : 0.0)
                   << ", \"caption\": \"ROUTE\", \"display\": \"" << (byp ? "OFF" : "ON") << "\"},\n";

                const float auxScaleNorm = uibind::normOf (sp.auxOutput, inst.param (sp.auxOutput));
                char auxScaleDisp[32];
                uibind::formatValue (synthyboy::paramTable()[sp.auxOutput], inst.param (sp.auxOutput), auxScaleDisp, sizeof auxScaleDisp);
                jf << "    \"Parameter " << (fieldsBase + 24) << "\": {\"norm\": " << auxScaleNorm
                   << ", \"caption\": \"SCALE\", \"display\": \"" << auxScaleDisp << "\"},\n";

                const bool auxInv = inst.param (sp.auxInv) > 0.5f;
                jf << "    \"Parameter " << (fieldsBase + 25) << "\": {\"norm\": " << (auxInv ? 1.0 : 0.0)
                   << ", \"caption\": \"INVERT\", \"display\": \"" << (auxInv ? "ON" : "OFF") << "\"},\n";

                const float auxCurveNorm = uibind::normOf (sp.auxPower, inst.param (sp.auxPower));
                char auxCurveDisp[32];
                uibind::formatValue (synthyboy::paramTable()[sp.auxPower], inst.param (sp.auxPower), auxCurveDisp, sizeof auxCurveDisp);
                jf << "    \"Parameter " << (fieldsBase + 26) << "\": {\"norm\": " << auxCurveNorm
                   << ", \"caption\": \"AUX CURVE\", \"display\": \"" << auxCurveDisp << "\"},\n";

                const float shapeCurveNorm = uibind::normOf (sp.power, inst.param (sp.power));
                char shapeCurveDisp[32];
                uibind::formatValue (synthyboy::paramTable()[sp.power], inst.param (sp.power), shapeCurveDisp, sizeof shapeCurveDisp);
                jf << "    \"Parameter " << (fieldsBase + 27) << "\": {\"norm\": " << shapeCurveNorm
                   << ", \"caption\": \"SHAPE CURVE\", \"display\": \"" << shapeCurveDisp << "\"},\n";

                const bool stereo = inst.param (sp.stereo) > 0.5f;
                jf << "    \"Parameter " << (fieldsBase + 28) << "\": {\"norm\": " << (stereo ? 1.0 : 0.0)
                   << ", \"caption\": \"STEREO\", \"display\": \"" << (stereo ? "ON" : "OFF") << "\"},\n";

                const float riseNorm = uibind::normOf (sp.rise, inst.param (sp.rise));
                char riseDisp[32];
                uibind::formatValue (synthyboy::paramTable()[sp.rise], inst.param (sp.rise), riseDisp, sizeof riseDisp);
                jf << "    \"Parameter " << (fieldsBase + 29) << "\": {\"norm\": " << riseNorm
                   << ", \"caption\": \"RISE\", \"display\": \"" << riseDisp << "\"},\n";

                const float fallNorm = uibind::normOf (sp.fall, inst.param (sp.fall));
                char fallDisp[32];
                uibind::formatValue (synthyboy::paramTable()[sp.fall], inst.param (sp.fall), fallDisp, sizeof fallDisp);
                jf << "    \"Parameter " << (fieldsBase + 30) << "\": {\"norm\": " << fallNorm
                   << ", \"caption\": \"FALL\", \"display\": \"" << fallDisp << "\"},\n";

                const int delayParam = inst.nativeView == 4 ? sp.delayBeats : sp.delay;
                const float delayNorm = uibind::normOf (delayParam, inst.param (delayParam));
                char delayDisp[32];
                uibind::formatValue (synthyboy::paramTable()[delayParam], inst.param (delayParam), delayDisp, sizeof delayDisp);
                jf << "    \"Parameter " << (fieldsBase + 31) << "\": {\"norm\": " << delayNorm
                   << ", \"caption\": \"" << (inst.nativeView == 4 ? "BEATS" : "DELAY") << "\", \"display\": \"" << delayDisp << "\"},\n";

                const bool delaySync = inst.param (sp.delaySync) > 0.5f;
                jf << "    \"Parameter " << (fieldsBase + 32) << "\": {\"norm\": " << (delaySync ? 1.0 : 0.0)
                   << ", \"caption\": \"SYNC\", \"display\": \"" << (delaySync ? "ON" : "OFF") << "\"},\n";

                const int detBase = base + synthyboy_mpc::nativeui::kModDetailControl;
                const auto srcChoice = static_cast<int> (std::lround (inst.param (sp.source)));
                const auto dstChoice = static_cast<int> (std::lround (inst.param (sp.destination)));
                const auto auxChoice = static_cast<int> (std::lround (inst.param (sp.auxSource)));
                const char* srcName = srcChoice > 0 ? ModUi::sourceName (srcChoice) : "---";
                const char* dstName = dstChoice > 0 ? ModUi::destName (dstChoice) : "---";
                const char* auxName = auxChoice > 0 ? ModUi::sourceName (auxChoice) : "NONE";
                const bool armed = ui.detail().isDeleteConfirmed();

                jf << "    \"Parameter " << (detBase + 0) << "\": {\"norm\": 0.0, \"caption\": \"BACK\", \"display\": \"BACK\"},\n";
                jf << "    \"Parameter " << (detBase + 1) << "\": {\"norm\": " << (armed ? 1.0 : 0.0) << ", \"caption\": \"DELETE\", \"display\": \"" << (armed ? "CONFIRM?" : "DELETE") << "\"},\n";
                jf << "    \"Parameter " << (detBase + 2) << "\": {\"norm\": 0.5, \"caption\": \"SOURCE\", \"display\": \"" << srcName << "\"},\n";
                jf << "    \"Parameter " << (detBase + 3) << "\": {\"norm\": 0.5, \"caption\": \"TARGET\", \"display\": \"" << dstName << "\"},\n";
                jf << "    \"Parameter " << (detBase + 4) << "\": {\"norm\": 0.0, \"caption\": \"SOURCE\", \"display\": \"" << auxName << "\"},\n";
                jf << "    \"Parameter " << (detBase + 5) << "\": {\"norm\": " << (armed ? 1.0 : 0.0) << ", \"caption\": \"Confirm\", \"display\": \"" << (armed ? "CONFIRM?" : "") << "\"},\n";
            }

            const int boardBase = base + synthyboy_mpc::nativeui::kModBoardControl;
            const float boardGateNorm = totalActive == 0 ? 1.0f : 0.0f;
            jf << "    \"Parameter " << (boardBase + synthyboy_mpc::nativeui::modboard::Gate) << "\": {\"norm\": "
               << boardGateNorm << ", \"caption\": \"Board Gate\", \"display\": \"" << (totalActive == 0 ? "1" : "0") << "\"}\n";

            jf << "  },\n";
            jf << "  \"params\": {\n";
            for (size_t p = 0; p < synthyboy::paramCount(); ++p)
            {
                const auto& pi = synthyboy::paramTable()[p];
                const float plain = inst.param (static_cast<int> (p));
                const float norm = uibind::normOf (static_cast<int> (p), plain);
                char disp[64] {};
                uibind::formatValue (pi, plain, disp, sizeof disp);
                jf << "    \"" << p << "\": {\"key\": \"" << pi.key << "\", \"name\": \"" << pi.name
                   << "\", \"plain\": " << plain << ", \"norm\": " << norm << ", \"value\": \"" << disp << "\"}"
                   << (p + 1 < synthyboy::paramCount() ? ",\n" : "\n");
            }
            jf << "  }\n";
            jf << "}\n";
        }
    }

    return 0;
}
