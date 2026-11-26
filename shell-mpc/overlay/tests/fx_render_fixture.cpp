// SynthyBoy - real FX scene and matching host handle state for offline previews.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <fstream>
#include <iomanip>
#include <map>
#include "../fx_ui.h"
#include "demo_common.h"
using namespace synthyboy_mpc;
using namespace synthyboy_mpc::overlay;
namespace {
struct Instance : demo::Inst
{
    nativeui::FxCellBank cells;
    nativeui::fxheader::Bank header;
    nativeui::fxchain::Bank chain;
    void setNativeFxCells (int layout, const nativeui::FxCell* fields) noexcept override { cells.publish (layout, fields); }
    void setNativeFxHeader (const nativeui::fxheader::State& state) noexcept override { header.publish (state, generation); }
    void setNativeFxChain (const nativeui::fxchain::State& state) noexcept override { chain.publish (state, generation); }
};
std::string text (int source, float plain)
{
    if (source < 0) return {};
    const auto& pi = synthyboy::paramTable()[source];
    if (pi.choices && pi.numChoices)
        return ui::shownChoice (pi.key, std::clamp (static_cast<int> (std::lround (plain-pi.min)), 0, pi.numChoices-1), pi.choices[std::clamp (static_cast<int> (std::lround (plain-pi.min)), 0, pi.numChoices-1)]);
    char buf[64] {};
    uibind::formatValue (pi, plain, buf, sizeof buf);
    return buf;
}
struct Value { float norm; std::string display, name; };
}
int main (int argc, char** argv)
{
    if (argc != 6) { std::fprintf (stderr, "usage: fx_render_fixture kind tab limit overlay.rgba state.json\n"); return 2; }
    const int kind = std::atoi (argv[1]), panel = std::atoi (argv[2]), limit = std::atoi (argv[3]);
    if (kind < 1 || kind >= synthyboy::kNumFxInstanceTypes || panel < 0 || panel >= ui::fxl::panelsOf (kind).count) return 2;
    ui::QGroup::setWideDevice (SYNTHYBOY_MPC_UI_GEN == 2);
    demo::Rig<FxUi, Instance> r;
    r.ui.setHostBackground (true);
    const auto& keys = synthyboy::fxInstanceKeys (0);
    float defaults[synthyboy::kFxInstanceParams] {};
    synthyboy::fxInstanceDefaults (kind, defaults);
    for (int i = 0; i < synthyboy::kFxInstanceParams; ++i) r.inst.setAt (keys.p[i], defaults[i]);
    r.inst.setAt (keys.type, static_cast<float> (kind));
    r.inst.setAt (keys.on, 1.0f);
    r.inst.setAt (keys.order, 32.0f);
    const auto& desc = synthyboy::fxTypeDescriptor (kind);
    auto set = [&] (const char* key, float plain)
    {
        const auto* pi = synthyboy::findParamByKey (key);
        if (! pi) return;
        const int source = static_cast<int> (pi-synthyboy::paramTable());
        for (int p = 0; p < desc.count; ++p)
            if (desc.source[p] == source)
                r.inst.setAt (keys.p[p], synthyboy::toNormalised (*pi, synthyboy::constrainValue (*pi, plain)));
    };
    if (kind == synthyboy::kFxTypeChorus)
    {
        set ("chorus.rate", .4f); set ("chorus.depth", 50); set ("chorus.feedback", 40);
        set ("chorus.voices", 4); set ("chorus.delay1", 2); set ("chorus.delay2", 8); set ("chorus.mix", 50);
    }
    if (kind == synthyboy::kFxTypeReverb)
    {
        set ("reverb.mix", 35); set ("reverb.size", 65); set ("reverb.decay", 2400.0f);
        set ("reverb.chorus_amount", 25); set ("reverb.chorus_rate", .7f); set ("reverb.type", 4);
    }
    if (kind == synthyboy::kFxTypeCompressor)
    {
        set ("compressor.limit", static_cast<float> (limit)); set ("compressor.bands", 0);
        set ("compressor.mid.upper_threshold", -20); set ("compressor.mid.upper_ratio", 50);
        set ("compressor.mid.lower_ratio", 25);
    }
    if (kind == synthyboy::kFxTypeDistortion && limit != 0)   // the third argument is the drive in dB; with it the filter is on, to show its controls
    {
        set ("distortion.drive", static_cast<float> (limit)); set ("distortion.mix", 100); set ("distortion.filter_order", 2);
    }
    r.refresh (2);
    r.ui.screen().slotEditor.setPage (panel);
    r.refresh (2);
    auto scene = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder sb (*scene);
    sb.begin (gpu::kKindPlay, 0, 1280, 800);
    r.ui.draw (sb);
    if (sb.overflow) return 3;
    std::vector<uint32_t> pixels;
    renderScene (*scene, pixels);
    std::ofstream rgba (argv[4], std::ios::binary);
    for (int y = 110; y < 738; ++y)
        for (int x = 0; x < 1280; ++x)
        {
            const auto p = pixels[y*1280+x];
            const char channels[] = { static_cast<char> (p >> 16), static_cast<char> (p >> 8), static_cast<char> (p), static_cast<char> (p >> 24) };
            rgba.write (channels, 4);
        }
    const int base = static_cast<int> (synthyboy::paramCount()) + 82;
    std::map<int, Value> values;
    auto value = [&] (int offset, float norm, std::string display = {}, std::string name = {}) { values[base+offset] = { norm, display, name }; };
    value (nativeui::kFxLayout, static_cast<float> (r.inst.cells.layout())/nativeui::kFxLayoutLast);
    const auto& editor = r.ui.screen().slotEditor;
    for (int slot = 0; slot < nativeui::kFxCells; ++slot)
    {
        const auto cell = r.inst.cells.at (slot).cell();
        value (nativeui::kFxKindBase+slot, static_cast<float> (cell.kind)/nativeui::kFxKindLast);
        if (cell.target < 0) continue;
        const float norm = nativeui::fxUiValue (cell, r.inst.param (cell.target));
        char caption[64] {};
        const int p = editor.controlInCell (editor.firstRowOfPage()+slot/4, slot%4);
        editor.nameOf (p, caption, sizeof caption);
        const auto& pi = synthyboy::paramTable()[cell.source];
        Value shown {norm, text (cell.source, synthyboy::fromNormalised (pi, r.inst.param (cell.target))), caption};
        values[base+nativeui::kFxValueBase+slot] = shown;
        if (! nativeui::fxCellProxy (cell.kind)) values[base+nativeui::kFxDomainBase+cell.kind-nativeui::kFxFirstDomain] = shown;
    }
    const auto header = r.inst.header.state();
    r.inst.header.poll (-1.0f, [&] (int offset, float norm, bool) { value (nativeui::kFxHeader+offset, norm); });
    value (nativeui::kFxHeader+2+header.item*nativeui::fxheader::kFields+nativeui::fxheader::Rack,
           header.value (nativeui::fxheader::Rack), "MAIN", "RACK");
    value (nativeui::kFxBandBase+header.item, header.value (nativeui::fxheader::kBand));
    value (nativeui::kFxBandMode, static_cast<float> (header.bandMode)/3.0f);
    for (int offset = 0; offset < nativeui::fxchain::kHandles; ++offset)
    {
        std::string name;
        if (offset >= nativeui::fxchain::Select && offset < nativeui::fxchain::Gallery)
        {
            const bool selection = offset < nativeui::fxchain::Power;
            const auto card = r.inst.chain.card (offset - (selection ? nativeui::fxchain::Select : nativeui::fxchain::Power));
            if (card.kind > 0)
                name = selection ? synthyboy::fxTypeDescriptor (card.kind).shortName : ui::fxl::rackName (card.rack);
        }
        value (nativeui::kFxChain+offset, r.inst.chain.value (offset), name, name);
    }
    std::ofstream out (argv[5]);
    out << std::fixed << std::setprecision (7)
        << "{\"synthetic_fixture\":true,\"device_test\":false,\"base\":" << base
        << ",\"hardware_generation\":" << SYNTHYBOY_MPC_UI_GEN
        << ",\"settings_view\":0,\"tab_name\":\"FX\",\"kind\":" << kind << ",\"panel\":" << panel << ",\"params\":{},\"virtual\":{";
    bool first = true;
    for (const auto& [index, v] : values)
    {
        if (! first) out << ',';
        first = false;
        out << std::quoted ("Parameter "+std::to_string (index)) << ":{\"norm\":" << v.norm
            << ",\"display\":" << std::quoted (v.display) << ",\"name\":" << std::quoted (v.name) << '}';
    }
    out << "}}\n";
    return rgba && out ? 0 : 4;
}
