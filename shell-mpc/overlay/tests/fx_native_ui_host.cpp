// SynthyBoy - MPC standalone native FX controls and skin contract.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../fx_ui.h"
#include "demo_common.h"
#include "fx_native_contract.h"

using namespace synthyboy_mpc;
using namespace synthyboy_mpc::overlay;

namespace
{
int g_fails = 0;
void check (bool ok, const char* what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (! ok)
        ++g_fails;
}

struct NativeInst : demo::Inst
{
    nativeui::FxCellBank bank;
    nativeui::fxheader::Bank header;
    nativeui::fxchain::Bank chain;
    void setNativeFxHeader (const nativeui::fxheader::State& state) noexcept override { header.publish (state, generation); }
    bool takeNativeFxHeader (int field, const nativeui::fxheader::State& state, float& value) noexcept override
    {
        return header.take (field, state, value);
    }
    void writeHeader (int field, float value)
    {
        const auto state = header.state();
        header.report (header.at (field));
        if (field == nativeui::fxheader::Rack)
            header.setRackPopup (true);
        header.post (state.item, field, value, generation);
        if (field == nativeui::fxheader::Rack)
            header.setRackPopup (false);
    }
    void setNativeFxCells (int layout, const nativeui::FxCell* cells) noexcept override { bank.publish (layout, cells); }
    void setNativeFxChain (const nativeui::fxchain::State& state) noexcept override { chain.publish (state, generation); }
    void setNativeFxAddPopup (bool active) noexcept override { chain.setMenuOpen (active); }
    bool takeNativeFxChain (int field, const nativeui::fxchain::State& state, float& value) noexcept override
    {
        return chain.take (field, state, value);
    }
    void writeChain (int field, float value)
    {
        chain.poll ([] (int, float, bool) {});
        chain.post (field, value, generation);
    }
    bool takeNativeFxCell (int slot, const nativeui::FxCell& expected, float& value) noexcept override { return bank.take (slot, expected, value); }
    void writeCell (int cell, float value)
    {
        const auto snapshot = bank.at (cell);
        bank.report (snapshot);
        bank.post (snapshot, value);
    }
};
using FxRig = demo::Rig<FxUi, NativeInst>;

void addPool (NativeInst& inst, int slot, int kind)
{
    const auto& keys = synthyboy::fxInstanceKeys (slot);
    float values[synthyboy::kFxInstanceParams] {};
    synthyboy::fxInstanceDefaults (kind, values);
    for (int p = 0; p < synthyboy::kFxInstanceParams; ++p)
        inst.setAt (keys.p[p], values[p]);
    inst.setAt (keys.type, static_cast<float> (kind));
    inst.setAt (keys.on, 1.0f);
    inst.setAt (keys.order, static_cast<float> (32 + 64 * slot));
}

float valueOf (FxRig& r, int cell)
{
    const auto binding = r.inst.bank.at (cell).cell();
    const auto& source = synthyboy::paramTable()[binding.source];
    return synthyboy::fromNormalised (source, r.inst.param (binding.target));
}

bool binds (const FxRig& r, int slot, const char* key, int kind)
{
    const auto cell = r.inst.bank.at (slot).cell();
    const auto* source = synthyboy::findParamByKey (key);
    if (source == nullptr || cell.source != synthyboy::findParamIndex (source->id) || cell.kind != kind)
        return false;
    const int p = r.ui.screen().slotEditor.controlOfKey (key);
    return cell.target == r.ui.paramIndexOf (r.ui.selectedItem(), p);
}

void testContract ()
{
    bool consistent = true;
    const auto variants = fxcontract::widgets (consistent);
    check (consistent && ! variants.empty(), "all editor pages, dependent states and bands have exact skin cell geometry and unique live menu domains");
    const auto choices = fxcontract::domains (consistent);
    check (consistent && choices[nativeui::kFxCompressorMode][2] == "Peak"
           && choices[nativeui::kFxLaw][0] == "State variable" && choices[nativeui::kFxConvolveLaw][0] == "Relative"
           && choices[nativeui::kFxVoices].front() == "1" && choices[nativeui::kFxVoices].back() == "4",
           "native domains preserve named and integer choices from the editor");
    for (int model = 0; model < nativeui::kFxStyleModels; ++model)
    {
        const int source = demo::Inst::idx ("fxfilter.style");
        const auto& pi = synthyboy::paramTable()[source];
        const nativeui::FxCell style { source, source, nativeui::kFxFirstDomain + nativeui::kFxStyleBase + model };
        const int last = static_cast<int> (choices[nativeui::kFxStyleBase + model].size()) - 1;
        for (int i = 0; i <= last; ++i)
        {
            const float host = last > 0 ? static_cast<float> (i) / last : 0.0f;
            const float engine = nativeui::fxEngineValue (style, host);
            consistent &= synthyboy::fromNormalised (pi, engine) == static_cast<float> (i)
                       && std::fabs (nativeui::fxUiValue (style, engine) - host) < 1.0e-6f;
        }
    }
    check (consistent, "all 27 FX Filter models convert each named style to and from the actual engine index");
}

void testChorus ()
{
    FxRig r;
    auto& ed = r.ui.screen().slotEditor;
    r.refresh();
    check (r.inst.bank.layout() == 0 && ! r.inst.bank.report (r.inst.bank.domain (nativeui::kFxFirstDomain)), "gallery and absent menu endpoints are inert");
    addPool (r.inst, 0, synthyboy::kFxTypeChorus);
    addPool (r.inst, 1, synthyboy::kFxTypeChorus);
    r.refresh();
    check (binds (r, 0, "chorus.rate", nativeui::kFxDial) && binds (r, 1, "chorus.sync", nativeui::kFxFirstDomain + nativeui::kFxSync)
           && binds (r, 4, "chorus.voices", nativeui::kFxFirstDomain + nativeui::kFxVoices), "Chorus binds its Hz dial, named sync and whole-number voices at the expected positions");
    const auto rate = ed.cellRect (0, 0);
    const auto empty = ed.cellRect (2, 3);
    check (! r.ui.covers (rate.x + 5, rate.y + 5) && r.ui.covers (empty.x + 5, empty.y + 5), "only live editor cells yield pointer input to the skin");
    auto scene = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder sb (*scene);
    sb.begin (gpu::kKindPlay, 0, FxUi::kCanvasW, FxUi::kCanvasH);
    r.ui.draw (sb);
    std::vector<uint32_t> pixels;
    renderScene (*scene, pixels);
    bool clear = ! sb.overflow;
    for (int y = rate.y + 14; y < rate.bottom() - 14; ++y)
        for (int x = rate.x + 14; x < rate.right() - 14; ++x)
        {
            const auto pixel = pixels[static_cast<size_t> (y) * FxUi::kCanvasW + static_cast<size_t> (x)];
            if (clear && pixel >> 24 != 0)
                std::fprintf (stderr, "native aperture pixel %d,%d=%08x cell=%d,%d,%d,%d\n", x, y, pixel, rate.x, rate.y, rate.w, rate.h);
            clear &= pixel >> 24 == 0;
        }
    check ((pixels[static_cast<size_t> (FxUi::kTabY) * FxUi::kCanvasW] >> 24) == 0,
           "the active FX editor exposes the verified native page ground outside its controls");
    check (clear, "native Chorus RATE interior is transparent, with Gen1 frame/marker confined to its edges");
    check (r.inst.bank.at (0).cell().target == synthyboy::fxInstanceKeys (0).p[ed.controlOfKey ("chorus.rate")],
           "selected pool slot receives the write, independent of the fixed Chorus");
    r.inst.bank.post (r.inst.bank.at (0), 0.8f);
    r.refresh();
    check (r.inst.writes == 0, "an unreported binding rejects setter calls");
    r.inst.writeCell (0, 0.8f);
    r.refresh();
    check (r.inst.writes == 1 && r.inst.lastWritten == r.inst.bank.at (0).cell().target, "reported native dial writes exactly the selected pool parameter");
    r.inst.writeCell (1, 1.0f / 3.0f);
    r.refresh();
    check (binds (r, 0, "chorus.division", nativeui::kFxFirstDomain + nativeui::kFxLongDivision), "Tempo replaces Hz with a single named division in the same cell");
    r.inst.writeCell (4, 1.0f / 3.0f);
    r.refresh();
    check (valueOf (r, 4) == 2.0f, "voices selects an exact whole number");
    r.inst.writeCell (0, 1.0f);
    int before = r.inst.writes;
    r.inst.writeHeader (nativeui::fxheader::Page, 1.0f);
    r.refresh();
    check (ed.numPages() == 1 && binds (r, 6, "chorus.delay1", nativeui::kFxDial) && r.inst.writes == before + 1, "unused panel commands do not alter Chorus routing");
    before = r.inst.writes;
    r.inst.writeCell (0, 0.8f);
    ++r.inst.generation;
    r.refresh();
    check (r.inst.writes == before, "preset generation invalidates a queued write even where the same pool key stays selected");
    r.inst.writeCell (0, 0.9f);
    r.inst.writeChain (nativeui::fxchain::Select + 1, 1.0f);
    r.refresh();
    check (r.ui.selectedItem().index == 1 && r.inst.writes == before, "switching to a second Chorus rejects the old slot's queued edit");
    r.inst.writeCell (0, 0.7f);
    r.inst.writeHeader (nativeui::fxheader::Power, 0.0f);
    r.refresh();
    before = r.inst.writes;
    check (r.inst.bank.layout() == 0 && ! ed.power.state.on && r.ui.covers (rate.x + 5, rate.y + 5)
           && r.inst.lastWritten == synthyboy::fxInstanceKeys (1).on,
           "power OFF conceals and disarms native cells while its native switch stays usable");
    r.inst.writeCell (0, 0.9f);
    r.refresh();
    check (r.inst.writes == before, "host movement on OFF writes nothing");
    r.inst.writeHeader (nativeui::fxheader::Power, 1.0f);
    r.refresh();
    check (r.inst.bank.layout() != 0 && ed.power.state.on, "power ON republishes the live cells");
    r.ui.setHostBackground (false);
    r.refresh();
    check (ed.nativeControls() && r.inst.bank.layout() != 0 && ! r.ui.covers (rate.x + 5, rate.y + 5),
           "losing the host background never revives the obsolete overlay controls");
    r.ui.setHostBackground (true);
    r.refresh();
    r.ui.leftTab (r.inst);
    check (r.inst.bank.layout() == 0, "hidden FX tab releases its native bindings");
}

void testNativeOwnershipDuringList ()
{
    FxRig r;
    r.ui.setNativeControls (false);
    addPool (r.inst, 0, synthyboy::kFxTypeChorus);
    r.refresh();
    const auto add = r.ui.screen().chainBar.plusCard.rect();
    r.tapAt (add.x + add.w / 2, add.y + add.h / 2);
    check (r.ui.listOpen(), "legacy peek opens its retained modal type list");
    r.ui.setNativeControls (true);
    r.refresh();
    check (r.ui.listOpen() && r.ui.screen().nativeControls()
           && r.ui.screen().slotEditor.nativeControls(),
           "modal list does not revive the unrelated GPU base controls");
    check (r.inst.bank.layout() == 0 && r.inst.header.state().layout == nativeui::fxheader::Hidden
           && r.inst.chain.value (nativeui::fxchain::View) == 0.0f,
           "modal list disarms native cells, header and chain without changing control ownership");
    const int cards = r.ui.numCards();
    const auto row = r.ui.list().rowRect (0);
    r.tapAt (row.x + row.w / 2, row.y + row.h / 2);
    r.refresh();
    check (! r.ui.listOpen() && r.ui.numCards() == cards + 1 && r.ui.screen().nativeControls()
           && r.inst.bank.layout() != 0,
           "choosing a modal type still adds its effect and restores native bindings");
    r.ui.setNativeControls (false);
    r.refresh();
    const auto again = r.ui.screen().chainBar.plusCard.rect();
    r.tapAt (again.x + again.w / 2, again.y + again.h / 2);
    r.ui.setNativeControls (true);
    r.refresh();
    const int beforeBack = r.ui.numCards();
    r.stack.back();
    r.refresh();
    check (! r.ui.listOpen() && r.ui.numCards() == beforeBack && r.ui.screen().nativeControls()
           && r.inst.bank.layout() != 0,
           "Back cancels the modal without editing the chain and restores native bindings");
}

void testDelayAndCompressor ()
{
    FxRig d;
    addPool (d.inst, 0, synthyboy::kFxTypeDelay);
    d.refresh();
    auto& ed = d.ui.screen().slotEditor;
    auto set = [&] (const char* key, float plain)
    {
        const int p = ed.controlOfKey (key);
        d.inst.setAt (d.ui.paramIndexOf (d.ui.selectedItem(), p), synthyboy::fxInstanceNormalise (ed.currentType(), p, plain));
    };
    set ("delay.style", 1.0f);
    set ("delay.sync", 1.0f);
    d.refresh();
    const auto left = d.inst.bank.domain (nativeui::kFxFirstDomain + nativeui::kFxShortDivision);
    const auto right = d.inst.bank.domain (nativeui::kFxFirstDomain + nativeui::kFxDelayRightDivision);
    check (left.slot >= 0 && right.slot >= 0 && left.slot != right.slot && left.cell().target != right.cell().target,
           "the stereo Delay offers independent menus using the same division names");
    if (right.slot >= 0)
    {
        d.inst.writeCell (right.slot, 1.0f);
        d.refresh();
        check (d.inst.writes == 1 && d.inst.lastWritten == right.cell().target && valueOf (d, right.slot) == 8.0f,
               "choosing the right Delay division writes only the right value");
    }
    const auto graph = ed.displayRect();
    check (d.ui.covers (graph.x + graph.w / 2, graph.y + graph.h / 2), "the Delay graph remains overlay-owned");
    for (int rack = 1; rack <= 2; ++rack)
    {
        if (right.slot >= 0)
            d.inst.writeCell (right.slot, 0.5f);
        const float rightBeforeMove = right.slot >= 0 ? d.inst.param (right.cell().target) : -1.0f;
        d.inst.writeHeader (nativeui::fxheader::Rack, static_cast<float> (rack) / 2);
        d.refresh();
        const auto link = d.inst.bank.domain (nativeui::kFxFirstDomain + nativeui::kFxDelayRightDivision);
        const auto display = ed.displayRect();
        check (d.inst.param (synthyboy::fxInstanceKeys (0).rack) == static_cast<float> (rack)
               && link.slot >= 0 && link.cell().target == right.cell().target
               && d.inst.param (right.cell().target) == rightBeforeMove && d.ui.covers (display.x + display.w / 2, display.y + display.h / 2),
               "standalone rack changes retain the selected Delay's right division and overlay graph");
        if (link.slot >= 0)
        {
            const int before = d.inst.writes;
            d.inst.writeCell (link.slot, static_cast<float> (rack) / 8);
            d.refresh();
            check (d.inst.writes == before + 1 && d.inst.lastWritten == right.cell().target && valueOf (d, link.slot) == static_cast<float> (rack),
                   "the native menu edits the same pool parameter on the new bus");
        }
    }

    FxRig c;
    addPool (c.inst, 0, synthyboy::kFxTypeCompressor);
    c.refresh();
    auto& comp = c.ui.screen().slotEditor;
    const auto band = comp.nativeBandRect();
    check (comp.nativeBandShown() && ! comp.bandSel.enabled() && ! c.ui.covers (band.x + band.w / 2, band.y + band.h / 2)
           && binds (c, 5, "compressor.mid.upper_threshold", nativeui::kFxBipolar),
           "the compressor yields the view-local BAND selector and the mid threshold to the native skin");
    c.inst.writeHeader (nativeui::fxheader::kBand, 1.0f);
    c.refresh();
    check (binds (c, 5, "compressor.high.upper_threshold", nativeui::kFxBipolar), "changing BAND retargets the same threshold cell to the high band");
    c.inst.writeHeader (nativeui::fxheader::Page, 0.5f);
    c.refresh();
    check (comp.pageIndex() == 1 && comp.hasDisplay() && binds (c, 0, "compressor.level", nativeui::kFxBipolar),
           "Compressor BANDS exposes output and crossovers without replacing the header");
}

void testPeek ()
{
    for (int rack = 0; rack < 3; ++rack)
    {
        NativeInst inst;
        addPool (inst, 0, synthyboy::kFxTypeChorus);
        inst.setAt (synthyboy::fxInstanceKeys (0).rack, static_cast<float> (rack));
        FxUi peek (rack);
        peek.setNativeControls (true);
        UiStack stack;
        stack.openFull (FxUi::kElementPage);
        peek.update (inst, stack, 0.0, {});
        const auto point = peek.screen().slotEditor.cellRect (0, 0);
        const auto power = peek.screen().slotEditor.power.rect();
        check (inst.bank.layout() == 0 && inst.header.state().layout == nativeui::fxheader::Hidden
               && ! peek.nativeControls() && peek.covers (point.x + 5, point.y + 5)
               && peek.covers (power.x + 8, power.y + 8),
               "Mixer PEEK owns the editor in each rack without publishing standalone native cells");
    }
}

void testNativeChain ()
{
    namespace c = nativeui::fxchain;
    namespace h = nativeui::fxheader;
    FxRig r;
    auto& screen = r.ui.screen();
    auto choose = [&] (int kind) {
        r.inst.writeChain (c::Gallery + kind - 1, 0.0f);
        r.refresh();
    };
    auto mode = [&] (c::Mode expected) { return r.inst.chain.value (c::View) == static_cast<float> (expected) / (c::kModes - 1); };
    auto owns = [&] (ui::Rect rect) { return ! r.ui.covers (rect.x + rect.w / 2, rect.y + rect.h / 2); };
    r.refresh();
    const auto card = screen.gallery.cardRectOfType (synthyboy::kFxTypeChorus);
    check (mode (c::MainGallery) && screen.gallery.numTypes() == c::kTypes && owns (card) && owns (screen.chainBar.nativeAddRect()),
           "the empty chain offers all sixteen types and yields the gallery cards and MAIN Add menu to the host");
    auto scene = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder sb (*scene);
    sb.begin (gpu::kKindPlay, 0, FxUi::kCanvasW, FxUi::kCanvasH);
    r.ui.draw (sb);
    std::vector<uint32_t> pixels;
    renderScene (*scene, pixels);
    const int labelY = card.bottom() - 24;
    bool clearLabel = ! sb.overflow;
    for (int y = labelY - 14; y < labelY + 14; ++y)
        for (int x = card.x + 3; x < card.right() - 3; ++x)
            clearLabel &= pixels[y * FxUi::kCanvasW + x] == 0u;
    const auto plot = screen.gallery.pictureRect (0);
    check (clearLabel && pixels[(plot.y + plot.h / 2) * FxUi::kCanvasW + plot.x + 20] != 0u,
           "the native gallery label is transparent in the foreground and the custom preview curve still draws");
    r.tapAt (card.x + 1, card.y + 1);
    r.inst.writeChain (c::Menu, static_cast<float> (synthyboy::kFxTypeChorus) / c::kTypes);
    r.refresh();
    check (r.inst.writes == 0, "a raw overlay gallery tap and an unmarked menu callback cannot add effects");
    choose (synthyboy::kFxTypeChorus);
    const int added = r.inst.writes;
    choose (synthyboy::kFxTypeChorus);
    check (added > 0 && r.inst.writes == added && r.ui.numCards() == 1 && mode (c::MainAdd),
           "one gallery click adds one effect and a repeated click cannot add another after the editor opens");
    const auto& keys = synthyboy::fxInstanceKeys (r.ui.selectedItem().index);
    r.inst.writeChain (c::Power, 0.0f);
    r.refresh();
    check (r.inst.writes == added + 1 && r.inst.lastWritten == keys.on && r.inst.param (keys.on) == 0.0f
           && owns (screen.chainBar.nativePowerRect (0)) && owns (screen.chainBar.nativeSelectRect (0))
           && ! screen.chainBar.nativeDragEnabled, "the chain routes OFF to exactly the selected effect's enable parameter");
    r.inst.writeChain (c::Power, 1.0f);
    r.refresh();
    r.inst.writeHeader (h::Remove, 1.0f);
    r.refresh();
    check (r.ui.numCards() == 0 && mode (c::MainGallery) && r.inst.bank.layout() == 0 && owns (card),
           "removing the last effect immediately restores the native gallery");

    choose (ui::fxl::kKindSplitterLh);
    r.inst.set ("reverb.on", 1.0f);
    r.refresh();
    r.tap (screen.slotEditor.bands.lane[0].tile);
    check (mode (c::FilteredGallery) && owns (screen.chainBar.nativeBackRect())
           && r.inst.chain.value (c::Gallery + synthyboy::kFxTypeReverb - 1) == 0.0f
           && r.inst.chain.value (c::Gallery + ui::fxl::kKindSplitterLh - 1) == 0.0f,
           "an empty band gallery excludes active fixed modules and splitters but retains Back");
    const int beforeUnavailable = r.inst.writes;
    choose (synthyboy::kFxTypeReverb);
    check (r.inst.writes == beforeUnavailable, "a hidden unavailable band card rejects commands");
    choose (synthyboy::kFxTypeChorus);
    check (mode (c::FilteredAdd) && r.inst.param (demo::Inst::idx ("chorus.rack")) == static_cast<float> (r.ui.scope()),
           "a band gallery adds the free fixed Chorus to the opened band");
    r.inst.writeChain (c::Choose, 1.0f);
    r.refresh();
    const int beforeCancel = r.inst.writes;
    r.ui.turnQ (0, 0.25, r.stack, r.inst);
    r.inst.writeChain (c::Back, 0.0f);
    r.refresh();
    check (mode (c::FilteredAdd) && r.inst.writes == beforeCancel, "Back cancels the native band gallery and Q cannot edit the covered editor");
    r.inst.writeChain (c::Back, 0.0f);
    r.refresh();
    check (r.ui.scope() == 0 && r.inst.writes == beforeCancel, "a second Back returns to the splitter editor with no writes");
}

void testNativeChainCapacityAndDrag ()
{
    namespace c = nativeui::fxchain;
    FxRig r;
    r.refresh();
    for (int i = 0; i < c::kSlots; ++i)
        addPool (r.inst, i, synthyboy::kFxTypeChorus);
    r.refresh();
    auto& chain = r.ui.screen().chainBar;
    check (r.ui.numCards() == 16 && r.inst.chain.value (c::Count) == 1.0f
           && r.inst.chain.value (c::View) == static_cast<float> (c::FilteredAdd) / (c::kModes - 1),
           "a full pool retains its sixteen controls and exposes the filtered add path");
    r.inst.writeChain (c::Choose, 1.0f);
    r.refresh();
    check (r.ui.screen().gallery.numTypes() == 3 && r.inst.chain.value (c::Gallery) == 0.0f
           && r.inst.chain.value (c::Gallery + ui::fxl::kKindSplitterLh - 1) == 1.0f,
           "with no free pool slot the gallery offers only the free splitter types");
    r.inst.writeChain (c::Back, 0.0f);
    r.refresh();
    for (int i = 0; i < c::kSlots; ++i)
        for (auto target : { chain.nativePowerRect (i), chain.nativeSelectRect (i) })
            check (target.w >= 56 && target.h >= 56 && target.x >= 0 && target.right() <= FxUi::kCanvasW
                   && ! r.ui.covers (target.x + target.w / 2, target.y + target.h / 2),
                   "each full-chain cell is reachable through a native target of at least 56 pixels");
    r.inst.writeChain (c::Select + 15, 1.0f);
    r.refresh();
    check (r.ui.selectedItem().index == 15, "the far-right card selects the sixteenth pool effect");
    const auto drag = chain.dragRect();
    const auto left = chain.nativeSlotRect (0);
    const int x = drag.x + drag.w / 2, y = drag.y + drag.h / 2;
    const int before = r.inst.writes;
    r.drag (x, y, FxUi::kCanvasW + 12, y, true);
    check (r.inst.writes == before && r.ui.selectedItem().index == 15, "lifting off-screen cancels the reorder drag");
    r.drag (x, y, left.x + 10, y, true);
    check (r.inst.writes > before && r.ui.cardItem (0).index == 15, "dragging the selected effect onto the first slot reorders it");
    const int after = r.inst.writes;
    r.drag (x, y, left.x + 10, y, false);
    ++r.inst.generation;
    r.refresh();
    TouchEvent up; up.kind = TouchEvent::Up; up.x = chain.nativeSlotRect (8).x + 10; up.y = y;
    r.ui.touch (up, r.stack, r.inst, r.now);
    check (r.inst.writes == after, "preset replacement cancels an in-flight chain reorder");
    for (int kind = 1; kind < ui::fxl::kKindSplitterLh; ++kind)
        r.inst.setAt (synthyboy::fxTypeDescriptor (kind).onIndex, 1.0f);
    for (const char* key : { "splitter_lh.on", "splitter_lmh.on", "splitter_ms.on" })
        r.inst.set (key, 1.0f);
    r.refresh();
    const auto nav = chain.nativePageRect();
    check (r.ui.numCards() == 32 && r.inst.chain.value (c::Page) == 0.5f
           && ! r.ui.covers (nav.x, nav.y) && r.ui.covers (chain.nativeAddRect().x, chain.nativeAddRect().y),
           "maximum chain pagination exposes all thirty-two effects and hides Add");
    const int beforePage = r.inst.writes;
    const auto selectedBeforePage = r.ui.selectedItem();
    r.inst.writeChain (c::Page, 1.0f);
    r.inst.writeChain (c::Power, 0.0f);
    r.refresh();
    check (r.inst.chain.value (c::Page) == 1.0f && r.inst.chain.value (c::Count) == 1.0f && r.inst.writes == beforePage,
           "Page 1 to 2 takes priority over a same-tick POWER from the first window");
    r.inst.writeChain (c::Page, 0.5f);
    r.inst.writeChain (c::Select + 15, 1.0f);
    r.refresh();
    check (r.inst.chain.value (c::Page) == 0.5f && r.ui.selectedItem() == selectedBeforePage && r.inst.writes == beforePage,
           "Page 2 to 1 discards a same-tick SELECT from the other window");
    r.inst.writeChain (c::Page, 1.0f);
    r.refresh();
    const auto last = r.ui.cardItem (31);
    r.inst.writeChain (c::Select + 15, 1.0f);
    r.refresh();
    check (r.ui.selectedItem() == last, "the last of thirty-two effects is selectable through the second page");
    const int beforeCrossMove = r.inst.writes;
    r.drag (x, y, nav.x + nav.w / 2, nav.y + nav.h / 2, false);
    up.x = left.x + 10; up.y = y;
    r.ui.touch (up, r.stack, r.inst, r.now);
    r.refresh();
    check (r.inst.writes > beforeCrossMove && r.ui.cardItem (0) == last,
           "a held MOVE can cross the page boundary and reorder against an offscreen target");
}

void testNativeChainReports ()
{
    namespace c = nativeui::fxchain;
    c::Bank chain;
    std::array<float, c::kHandles> last {};
    auto tick = [&]
    {
        int changes = 0;
        chain.poll ([&] (int field, float value, bool force)
        {
            changes += last[field] != value || force;
            last[field] = value;
        });
        return changes;
    };
    check (tick() == 0, "a hidden FX chain sends zero parameter reports");
    c::State gallery;
    gallery.mode = c::MainGallery;
    gallery.offered = (1u << c::kTypes) - 1;
    chain.publish (gallery, 1);
    check (tick() == 18 && tick() == 0, "opening the gallery reports sixteen types, Add and the view once");
    c::State editor;
    editor.mode = c::MainAdd;
    editor.count = editor.selected = 1;
    editor.cards[0] = { synthyboy::kNumFxSlots, synthyboy::kFxTypeChorus, 0, true };
    chain.publish (editor, 1);
    check (tick() == 21 && tick() == 0, "choosing the first card reports the two live card handles and clears the gallery once");
}

void testHeaderReportTraffic ()
{
    namespace h = nativeui::fxheader;
    h::Bank header;
    std::array<float, h::kHandles> reported {};
    auto tick = [&]
    {
        std::vector<int> reports;
        header.poll (reported[0], [&] (int field, float value, bool force)
        {
            if (force || value != reported[static_cast<size_t> (field)])
            {
                reports.push_back (field);
                reported[static_cast<size_t> (field)] = value;
            }
        });
        return reports;
    };
    check (tick().empty(), "the hidden header emits no host reports");
    header.publish ({ 0, synthyboy::kFxTypeDelay, h::MultiOn, 0 }, 0);
    check (tick() == std::vector<int> { 2, 3, 4, 5, 0, 1 } && tick().empty(),
           "opening FX reports four actions and two frame handles, with zero reports on the unchanged next tick");
    const int second = 16 * h::kFields + 2;
    header.publish ({ 16, synthyboy::kFxTypeChorus, h::MultiOn, 0 }, 0);
    check (tick() == std::vector<int> { 1, second, second + 1, second + 2, second + 3, 0, 1 },
           "switching source reports one hide, only four replacement actions, and the two new frame handles");
    header.publish ({}, 0);
    check (tick() == std::vector<int> { 1, 0 } && tick().empty(), "leaving FX reports only the two visibility handles");
}

void testHeaderActions ()
{
    namespace h = nativeui::fxheader;
    FxRig r;
    addPool (r.inst, 0, synthyboy::kFxTypeReverb);
    addPool (r.inst, 1, synthyboy::kFxTypeDelay);
    r.refresh();
    const auto& ed = r.ui.screen().slotEditor;
    const int first = r.inst.header.state().item;
    check (first == synthyboy::kNumFxSlots && r.inst.header.state().rackCount() == 3,
           "the first pool slot owns a source-keyed three-rack header");
    auto scene = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder sb (*scene);
    sb.begin (gpu::kKindPlay, 0, FxUi::kCanvasW, FxUi::kCanvasH);
    r.ui.draw (sb);
    std::vector<uint32_t> pixels;
    renderScene (*scene, pixels);
    for (const ui::Component* c : { static_cast<const ui::Component*> (&ed.power), static_cast<const ui::Component*> (&ed.rack),
                                    static_cast<const ui::Component*> (&ed.tabs), static_cast<const ui::Component*> (&ed.remove) })
    {
        const auto rect = c->rect();
        const int x = rect.x + rect.w / 2, y = rect.y + rect.h / 2;
        check (! r.ui.covers (x, y) && pixels[y * FxUi::kCanvasW + x] == 0u,
               "active header touch and centre pixel belong exclusively to the host");
    }
    r.inst.writeHeader (h::Page, 1.0f);
    r.refresh();
    r.inst.writeHeader (h::Page, 1.0f);
    r.refresh();
    check (ed.isMoreView() && r.inst.writes == 0, "a duplicate MORE callback does not undo the new page");
    r.inst.writeHeader (h::Power, 0.0f);
    r.refresh();
    const auto off = r.inst.header.state();
    check (! off.on() && off.tabIndex() == 2 && off.cell (h::Rack).target < 0 && off.cell (h::Remove).target < 0
           && off.cell (h::Power).target == first && off.cell (h::Page).target == first,
           "OFF rejects RACK and REMOVE, while POWER and LESS remain operable");
    const int offWrites = r.inst.writes;
    r.inst.writeHeader (h::Rack, 1.0f);
    r.inst.writeHeader (h::Remove, 1.0f);
    r.inst.writeHeader (h::Page, 0.0f);
    r.refresh();
    check (! ed.isMoreView() && r.inst.writes == offWrites, "LESS is navigation and hidden OFF actions write nothing");
    r.inst.writeHeader (h::Power, 1.0f);
    r.refresh();

    r.inst.writeHeader (h::Rack, 1.0f);
    const int beforePreset = r.inst.writes;
    ++r.inst.generation;
    r.inst.header.post (first, h::Rack, 1.0f, r.inst.generation);
    addPool (r.inst, 0, synthyboy::kFxTypeReverb);
    r.refresh();
    const int rebound = r.inst.header.state().item;
    r.inst.header.report (r.inst.header.at (h::Rack));
    r.inst.header.post (first, h::Rack, 1.0f, r.inst.generation);
    r.refresh();
    check (rebound == first && r.inst.header.state().kind == synthyboy::kFxTypeReverb
           && r.inst.header.state().rack == 0 && r.inst.writes == beforePreset,
           "queued and late rack choices cannot move the same physical slot reused by the new preset");

    r.inst.writeHeader (h::Rack, 1.0f);
    r.inst.writeChain (nativeui::fxchain::Select + 1, 1.0f);
    r.refresh();
    const int secondItem = r.inst.header.state().item;
    r.inst.header.report (r.inst.header.at (h::Rack));
    r.inst.header.post (rebound, h::Rack, 1.0f, r.inst.generation);
    r.refresh();
    check (secondItem != rebound && r.inst.writes == beforePreset
           && r.inst.param (synthyboy::fxInstanceKeys (0).rack) == 0.0f && r.inst.param (synthyboy::fxInstanceKeys (1).rack) == 0.0f,
           "a late source-keyed rack callback cannot move either effect after changing selection");

    r.inst.writeHeader (h::Remove, 1.0f);
    r.refresh();
    const int afterRemove = r.inst.writes;
    r.inst.header.report (r.inst.header.at (h::Remove));
    r.inst.header.post (secondItem, h::Remove, 1.0f, r.inst.generation);
    r.refresh();
    check (afterRemove > beforePreset && r.inst.writes == afterRemove
           && r.inst.param (synthyboy::fxInstanceKeys (1).type) == synthyboy::kFxTypeNone
           && r.inst.param (synthyboy::fxInstanceKeys (0).type) == synthyboy::kFxTypeReverb,
           "repeating REMOVE after selection changed cannot remove the next effect");
    r.inst.writeHeader (h::Rack, 1.0f);
    namespace c = nativeui::fxchain;
    r.inst.setNativeFxAddPopup (true);
    r.inst.writeChain (c::Menu, static_cast<float> (synthyboy::kFxTypeChorus) / c::kTypes);
    r.inst.setNativeFxAddPopup (false);
    r.refresh();
    check (r.ui.numCards() == 2 && r.inst.header.state().rack == 0 && r.inst.writes > afterRemove,
           "the marked native Add menu cancels queued header writes when it adds a new slot");
    r.ui.leftTab (r.inst);
    check (r.inst.header.state().layout == h::Hidden, "a real tab leave unbinds the header");
}

void testFilterStyle ()
{
    FxRig r;
    addPool (r.inst, 0, synthyboy::kFxTypeFilter);
    r.refresh();
    const auto& ed = r.ui.screen().slotEditor;
    constexpr int styleKind = nativeui::kFxFirstDomain + nativeui::kFxStyleBase;
    const auto model = r.inst.bank.domain (nativeui::kFxFirstDomain + nativeui::kFxModel);
    if (model.slot >= 0)
    {
        r.inst.setAt (model.cell().target, 0.0f);
        r.refresh();
    }
    const auto analog = r.inst.bank.domain (styleKind);
    check (analog.slot >= 0 && model.slot >= 0, "FX Filter initially exposes the Analog STYLE and MODEL domains");
    if (analog.slot < 0 || model.slot < 0)
        return;
    r.inst.writeCell (analog.slot, 1.0f);
    r.inst.writeCell (model.slot, 4.0f / 26.0f);   // Diode
    r.refresh();
    const auto diode = r.inst.bank.domain (styleKind + 4);
    check (diode.slot >= 0 && diode.cell().target == analog.cell().target
           && r.inst.bank.domain (styleKind).slot < 0 && r.inst.writes == 1,
           "switching the model rejects a queued STYLE edit from the former domain");
    if (diode.slot >= 0)
    {
        r.inst.writeCell (diode.slot, 1.0f);
        r.refresh();
        check (r.inst.lastWritten == r.ui.paramIndexOf (r.ui.selectedItem(), ed.controlOfKey ("fxfilter.style"))
               && r.inst.writes == 2 && valueOf (r, diode.slot) == 1.0f,
               "choosing the final Diode style writes the valid engine index, rather than the final Analog style");
    }
}
void testFunctionalPanels()
{
    for (int kind = 1; kind < ui::fxl::kNumKinds; ++kind)
    {
        ui::FxFullScreen screen;
        screen.place ({ 0, 110, 1280, 628 });
        screen.setNativeControls (true);
        auto& ed = screen.slotEditor;
        ed.setType (kind, true);
        ed.setState ([] (const char* key) { const auto* p = synthyboy::findParamByKey (key); return p ? p->def : 0.0f; });
        const auto power = ed.power.rect(), rack = ed.rack.rect(), remove = ed.remove.rect();
        if (kind == synthyboy::kFxTypeChorus)
        {
            const bool wideBefore = ui::QGroup::wideDevice();
            ui::QGroup::setWideDevice (false);
            auto scene = std::make_unique<gpu::SceneData>();
            gpu::SceneBuilder sb (*scene);
            sb.begin (gpu::kKindPlay, 0, 1280, 800);
            ed.draw (sb);
            std::vector<uint32_t> pixels;
            renderScene (*scene, pixels);
            const auto frame = ed.groups[ed.activeRow()].rect();
            const bool drawn = pixels[frame.y * 1280 + frame.x + frame.w/2] != 0;
            check (drawn == ui::QGroup::hasQFrame(), "native FX draws the bank frame in Gen1 and compiles it out in Gen2");
            ui::QGroup::setWideDevice (wideBefore);
        }
        int seen[ui::FxSlotEditor::kMaxParams+1] {};
        for (int panel = 0; panel < ed.numPages(); ++panel)
        {
            ed.setPage (panel);
            check (ed.power.rect() == power && ed.rack.rect() == rack && ed.remove.rect() == remove,
                   "functional panel switching preserves the selected-effect header rectangles");
            if (ed.numPages() > 1)
                check (ed.tabs.rect().h >= 56 && ed.tabs.rect().w / ed.numPages() >= 56,
                       "each panel selector has an actual56px native hitbox");
            for (int row = 0; row < ed.rowsOnPage(); ++row)
                for (int col = 0; col < 4; ++col)
                {
                    const int p = ed.controlInCell (ed.firstRowOfPage()+row, col);
                    if (p < 0) continue;
                    ++seen[p];
                    const auto rect = ed.cellRect (row, col);
                    check (rect.y >= 110 && rect.bottom() <= 738 && rect.w >= 56 && rect.h >= 56,
                           "every panel cell stays inside the628px tab with real touch area");
                }
        }
        bool once = true;
        for (int p = 0; p <= ui::FxSlotEditor::kSel; ++p)
            once &= seen[p] <= 1;
        check (once, "no engine control is duplicated across functional panels");
        if (kind == synthyboy::kFxTypeChorus)
            check (ed.numPages() == 1 && ed.numRows() == 3, "all11 Chorus cells share one compact panel");
        if (kind == synthyboy::kFxTypeReverb)
            check (ed.numPages() == 3 && std::string (ed.panelName (1)) == "TONE" && std::string (ed.panelName (2)) == "MOD",
                   "Reverb has explicit MAIN TONE MOD panels");
    }
    FxRig r;
    addPool (r.inst, 0, synthyboy::kFxTypeReverb);
    r.refresh();
    auto& ed = r.ui.screen().slotEditor;
    for (int panel : { 1, 2, 0 })
    {
        r.inst.writeCell (0, .99f); // stale cell is invalidated by the source-bound panel action first
        r.inst.writeHeader (nativeui::fxheader::Page, panel / 2.0f);
        const int writes = r.inst.writes;
        r.refresh();
        check (ed.pageIndex() == panel && r.inst.header.state().tabIndex() == panel && r.inst.writes == writes,
               "all three native tab indices navigate without parameter writes or stale-cell application");
    }
    const auto pending = r.inst.header.at (nativeui::fxheader::Page);
    r.inst.header.report (pending);
    r.inst.header.post (r.inst.header.state().item, nativeui::fxheader::Page, 1.0f, r.inst.generation);
    ++r.inst.generation;
    addPool (r.inst, 0, synthyboy::kFxTypeChorus);
    r.refresh();
    check (ed.currentType() == synthyboy::kFxTypeChorus && ed.numPages() == 1 && ed.pageIndex() == 0,
           "replacing a preset/slot type rejects a queued tab action and clamps to the new MAIN panel");
}
} // namespace

int main (int argc, char** argv)
{
    if (argc == 2 && std::strcmp (argv[1], "--skin-contract") == 0)
        return fxcontract::exportManifest() ? 0 : 1;
    testContract();
    testFunctionalPanels();
    testChorus();
    testNativeOwnershipDuringList();
    testDelayAndCompressor();
    testPeek();
    testNativeChain();
    testNativeChainCapacityAndDrag();
    testNativeChainReports();
    testHeaderActions();
    testHeaderReportTraffic();
    testFilterStyle();
    std::printf (g_fails ? "fx_native_ui_host: FAILED (%d failures)\n" : "fx_native_ui_host: OK\n", g_fails);
    return g_fails ? 1 : 0;
}
