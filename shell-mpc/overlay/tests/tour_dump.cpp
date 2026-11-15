// SynthyBoy - MPC overlay: the function tour table. Lists every control of the oscillator UI (the compact view, the full screen, the detail pages, the route popup) of every instance and type with the
// centre and size of its touch rectangle, the id, the engine parameter and the kind of value, from the same OscUi code the plugin runs. The Key 61 script (scripts/tour on the device side) reads this and
// sends the touch that belongs to each control, then reads the parameter back. Usage: tour_dump > tour.txt   (one line per control: cfg|id|key|type|cx|cy|w|h|enabled|compact or full)
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <string>
#include <vector>

#include "../osc_ui.h"

using namespace synthyboy_mpc::overlay;

namespace
{
class Inst final : public IInstanceView
{
public:
    Inst() { const synthyboy::ParamInfo* t = synthyboy::paramTable(); for (size_t i = 0; i < synthyboy::paramCount(); ++i) v_[i] = t[i].def; }
    float param (int i) const noexcept override { return v_[i]; }
    bool live (synthyboy::LiveValues&) const noexcept override { return false; }
    double sampleRate() const noexcept override { return 48000.0; }
    int playSelection (int) const noexcept override { return sel; }
    void writeParam (int i, float norm) noexcept override { const synthyboy::ParamInfo& p = synthyboy::paramTable()[i]; v_[i] = synthyboy::constrainValue (p, synthyboy::fromNormalised (p, norm)); }
    void contentRoots (std::vector<ContentRoot>& out) const override { out.assign (1, { "SD CARD", "/sdcard" }); }
    static int idx (const std::string& key) { const auto* p = synthyboy::findParamByKey (key.c_str()); return p ? static_cast<int> (p - synthyboy::paramTable()) : -1; }
    void set (const std::string& key, float v) { v_[idx (key)] = v; }
    int sel = 2;

private:
    float v_[synthyboy::kMaxParams] = {};
};

void dump (const char* cfg, OscUi& ui, const std::vector<OscUi::Item>& items)
{
    synthyboy_mpc::ui::Component* leaves[64];
    const int nLeaves = ui.compact().collectLeaves (leaves, 0, 64);
    for (const OscUi::Item& it : items)
    {
        if (it.c == nullptr)
            continue;
        bool compact = false;
        for (int i = 0; i < nLeaves; ++i)
            compact = compact || leaves[i] == it.c || it.c == static_cast<synthyboy_mpc::ui::Component*> (&ui.compact().full);
        const synthyboy_mpc::ui::Rect r = it.c->rect();
        std::printf ("%s|%d|%s|%d|%d|%d|%d|%d|%s|%s\n", cfg, it.id, it.key.c_str(), static_cast<int> (it.type), r.x + r.w / 2, r.y + r.h / 2, r.w, r.h, it.c->enabled() && ! it.alwaysOff ? "on" : "off", compact ? "compact" : "full");
    }
}
}

int main()
{
    const char* names[5] = { "sub", "A", "B", "C", "noise" };
    for (int sel = 1; sel <= 5; ++sel)
        for (int raw : { 0, 4, 2 })
        {
            if ((sel == 1 || sel == 5) && raw != 0)
                continue;
            OscUi ui;
            Inst inst;
            UiStack stack;
            inst.sel = sel;
            if (sel >= 2 && sel <= 4)
                inst.set ("osc" + std::to_string (sel - 1) + ".type", static_cast<float> (raw));
            double now = 1000.0;
            auto refresh = [&] { now += 50.0; ui.update (inst, 1, stack, now, 1, 1); };
            refresh();
            const std::string tag = std::string (names[sel - 1]) + (sel >= 2 && sel <= 4 ? (raw == 0 ? "-wave" : raw == 4 ? "-multi" : "-grain") : "");
            dump (("main:" + tag).c_str(), ui, ui.items());
            stack.openFull (OscUi::kElementOsc);
            refresh();
            dump (("full:" + tag).c_str(), ui, ui.items());
            struct Pg { int page; const char* name; };
            for (const Pg& p : { Pg { OscUi::kPageUnison, "unison" }, Pg { OscUi::kPageGrain, "grain" }, Pg { OscUi::kPageEnvelope, "envelope" }, Pg { OscUi::kPageRoute, "route" } })
            {
                if (sel == 1 || sel == 5)
                    continue;
                UiStack s2 = stack;
                s2.openDetail (p.page);
                ui.update (inst, 1, s2, now += 50.0, 1, 1);
                dump (("page-" + std::string (p.name) + ":" + tag).c_str(), ui, ui.pageItems());
            }
            stack = UiStack();
            refresh();
        }
    return 0;
}
