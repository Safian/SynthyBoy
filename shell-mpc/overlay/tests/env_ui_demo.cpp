// SynthyBoy - MPC overlay: the DEMONSTRATION of the envelope UI (a host tool built by shell-mpc/accept.sh): renders the pages of EnvUi, driven by real engine parameters (an envelope set up like a pluck, a pad and a
// long swell), through the CPU reference renderer into PNG files in the directory given as the first argument: the compact view in every envelope colour, the full screen, the beats mode, the targets page, the list
// that picks a target, and the running dots in several places. One sub directory per look.
// Build: c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I shell-mpc/overlay -o env_ui_demo shell-mpc/overlay/tests/env_ui_demo.cpp <libsynthyboy_core.a ...> -lpthread
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <sys/stat.h>

#include "../env_ui.h"
#include "../scene_cpu.h"
#include "../../ui/tests/ui_test.h"

using namespace synthyboy_mpc;
using namespace synthyboy_mpc::overlay;

namespace
{
int g_images = 0, g_fails = 0;

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
    int playSelection (int g) const noexcept override { return g == 1 ? env : 1; }
    void writeParam (int i, float norm) noexcept override
    {
        const synthyboy::ParamInfo& p = synthyboy::paramTable()[i];
        v_[i] = synthyboy::constrainValue (p, synthyboy::fromNormalised (p, norm));
    }
    void setPlaySelection (int g, int i) noexcept override { if (g == 1) env = i; }
    static int idx (const std::string& key) { const auto* p = synthyboy::findParamByKey (key.c_str()); return p ? static_cast<int> (p - synthyboy::paramTable()) : -1; }
    void set (const std::string& key, float v) { v_[idx (key)] = v; }
    int env = 1;

private:
    float v_[synthyboy::kMaxParams] = {};
};

void save (const std::string& dir, const char* name, EnvUi& ui, int w = 1280, int h = 800)
{
    auto scene = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder sb (*scene);
    sb.begin (gpu::kKindPlay, 0, w, h);
    const ui::theme::Look& T = ui::theme::L();
    sb.clear (ui::theme::rgba (T.bg0, 255));
    sb.gradientRect (0.0f, 0.0f, static_cast<float> (w), static_cast<float> (h), ui::theme::rgba (T.bg0), ui::theme::rgba (T.bg1));
    if (ui.isMain())
        ui.drawCompact (sb);
    else
        ui.drawPage (sb);
    ui.drawLive (sb);
    ui.drawPopup (sb);
    uit::Image im;
    im.w = w; im.h = h;
    renderScene (*scene, im.px);
    const bool ok = ! sb.overflow && uit::writePng (dir + "/" + name, im, 1);
    std::printf ("%s  %s (%u ops)\n", ok ? "PASS" : "FAIL", name, scene->numOps);
    g_images += ok;
    g_fails += ! ok;
}

struct Rig
{
    EnvUi ui;
    Inst inst;
    UiStack stack;
    synthyboy::LiveValues live;
    bool haveLive = false;
    double now = 1000.0;
    void refresh() { now += 600.0; ui.update (inst, 2, stack, now, haveLive ? &live : nullptr); }
    void tap (const ui::Component& c)
    {
        TouchEvent e; e.kind = TouchEvent::Down; e.x = c.rect().x + c.rect().w / 2; e.y = c.rect().y + c.rect().h / 2;
        ui.touch (e, stack, inst, now);
        e.kind = TouchEvent::Up;
        ui.touch (e, stack, inst, now + 40.0);
        refresh();
    }
};

void setEnv (Inst& i, int n, float attack, float hold, float decay, float sustain, float release, float ac, float dc, float rc, float delay = 0.0f)
{
    const std::string p = "env" + std::to_string (n) + ".";
    i.set (p + "attack", attack); i.set (p + "hold", hold); i.set (p + "decay", decay); i.set (p + "sustain", sustain); i.set (p + "release", release);
    i.set (p + "attack_curve", ac); i.set (p + "decay_curve", dc); i.set (p + "release_curve", rc); i.set (p + "delay", delay);
}

void renderAll (const std::string& dir)
{
    {   // the compact view of the six envelopes, every one in its colour (ENV 1 is the ghost of the others)
        for (int n = 1; n <= 6; ++n)
        {
            Rig r;
            setEnv (r.inst, 1, 5.0f, 0.0f, 400.0f, 70.0f, 200.0f, 0.0f, -3.0f, -3.0f);
            const float t[6][9] = { { 5, 0, 400, 70, 200, 0, -3, -3, 0 }, { 120, 0, 1500, 40, 900, 4, -6, -2, 0 }, { 800, 200, 2400, 80, 3000, -5, -8, 6, 150 },
                                    { 2, 0, 90, 0, 60, 0, -9, -3, 0 }, { 2500, 400, 5000, 100, 6000, 8, 0, 4, 400 }, { 30, 60, 300, 55, 1200, 2, -2, -10, 0 } };
            setEnv (r.inst, n, t[n - 1][0], t[n - 1][1], t[n - 1][2], t[n - 1][3], t[n - 1][4], t[n - 1][5], t[n - 1][6], t[n - 1][7], t[n - 1][8]);
            r.inst.env = n;
            r.haveLive = true;
            r.live.envVoiceStage[n - 1][0] = 4.0f; r.live.envVoiceFraction[n - 1][0] = 0.35f;
            r.live.envVoiceStage[n - 1][1] = 6.0f; r.live.envVoiceFraction[n - 1][1] = 0.6f;
            r.refresh();
            const std::string name = "compact_env" + std::to_string (n) + ".png";
            save (dir, name.c_str(), r.ui);
        }
    }
    {   // the compact view with targets: ENV 1 and ENV 3 modulate something and carry the dot in their tab, the others carry no mark
        Rig r;
        setEnv (r.inst, 3, 800.0f, 200.0f, 2400.0f, 80.0f, 3000.0f, -5.0f, -8.0f, 6.0f, 150.0f);
        r.inst.env = 3;
        r.inst.set ("mod01.source", 3.0f);
        r.inst.set ("mod01.destination", static_cast<float> (synthyboy::modDestinationChoiceOf (static_cast<size_t> (Inst::idx ("filter1.cutoff")))));
        r.inst.set ("mod01.amount", 35.0f);
        r.inst.set ("mod02.source", 1.0f);
        r.inst.set ("mod02.destination", static_cast<float> (synthyboy::modDestinationChoiceOf (static_cast<size_t> (Inst::idx ("osc1.level")))));
        r.inst.set ("mod02.amount", 60.0f);
        r.refresh();
        save (dir, "compact_targets_dots.png", r.ui);
    }
    {   // the full screen of ENV 3 (a pad) with a voice in the decay and one in the release, the decay handle selected; ADSR group active
        Rig r;
        setEnv (r.inst, 3, 800.0f, 200.0f, 2400.0f, 80.0f, 3000.0f, -5.0f, -8.0f, 6.0f, 150.0f);
        r.inst.env = 3;
        r.inst.set ("env3.legato_inverted", 1.0f);
        r.inst.set ("mod01.source", 3.0f);
        r.inst.set ("mod01.destination", static_cast<float> (synthyboy::modDestinationChoiceOf (static_cast<size_t> (Inst::idx ("filter1.cutoff")))));
        r.inst.set ("mod01.amount", 35.0f);
        r.inst.set ("mod02.source", 3.0f);
        r.inst.set ("mod02.destination", static_cast<float> (synthyboy::modDestinationChoiceOf (static_cast<size_t> (Inst::idx ("osc1.level")))));
        r.inst.set ("mod02.amount", 100.0f);
        r.inst.set ("mod03.source", 3.0f);
        r.inst.set ("mod03.destination", static_cast<float> (synthyboy::modDestinationChoiceOf (static_cast<size_t> (Inst::idx ("osc2.unison.detune")))));
        r.inst.set ("mod03.amount", -12.0f);
        r.inst.set ("mod01.source", 3.0f);
        r.haveLive = true;
        r.live.envVoiceStage[2][0] = 4.0f; r.live.envVoiceFraction[2][0] = 0.4f;
        r.live.envVoiceStage[2][1] = 6.0f; r.live.envVoiceFraction[2][1] = 0.5f;
        r.live.envVoiceStage[2][2] = 2.0f; r.live.envVoiceFraction[2][2] = 0.5f;
        r.refresh();
        r.tap (r.ui.compact().full);
        r.ui.selectGraphHandle (ui::envgraph::Decay);
        r.refresh();
        save (dir, "full_screen.png", r.ui);
        r.tap (r.ui.full().aCurve);
        save (dir, "full_screen_shape_group.png", r.ui);
        r.inst.set ("env3.units", 1.0f);
        r.inst.set ("env3.attack_beats", 0.5f); r.inst.set ("env3.decay_beats", 2.0f); r.inst.set ("env3.release_beats", 1.5f); r.inst.set ("env3.hold_beats", 0.25f);
        r.refresh();
        save (dir, "full_screen_beats.png", r.ui);
        r.tap (r.ui.full().targets);
        save (dir, "targets_page.png", r.ui);
        r.tap (r.ui.targetsPage().add);
        save (dir, "targets_group_list.png", r.ui);
    }
    {   // the running dots: one voice through the stages (a sixth of a second apart)
        for (int k = 0; k < 6; ++k)
        {
            Rig r;
            setEnv (r.inst, 1, 400.0f, 300.0f, 1200.0f, 55.0f, 1800.0f, 3.0f, -4.0f, -4.0f);
            r.haveLive = true;
            r.live.envVoiceStage[0][0] = static_cast<float> (k + 1);
            r.live.envVoiceFraction[0][0] = k == 4 ? 0.0f : 0.5f;
            r.refresh();
            r.tap (r.ui.compact().full);
            const std::string name = "full_dot_stage" + std::to_string (k + 1) + ".png";
            save (dir, name.c_str(), r.ui);
        }
    }
}
} // namespace

int main (int argc, char** argv)
{
    const std::string base = argc > 1 ? argv[1] : ".";
    mkdir (base.c_str(), 0755);
    for (int look = 1; look <= 3; ++look)
    {
        ui::theme::setLook (look);
        const std::string dir = base + "/look-" + std::to_string (look);
        mkdir (dir.c_str(), 0755);
        renderAll (dir);
    }
    {   // the reduced level of detail (the Key 61): the compact and the full screen
        ui::theme::setLook (1);
        ui::theme::setDetail (2);
        const std::string dir = base + "/look-1-reduced";
        mkdir (dir.c_str(), 0755);
        Rig r;
        setEnv (r.inst, 2, 120.0f, 0.0f, 1500.0f, 40.0f, 900.0f, 4.0f, -6.0f, -2.0f);
        r.inst.env = 2;
        r.haveLive = true;
        r.live.envVoiceStage[1][0] = 4.0f; r.live.envVoiceFraction[1][0] = 0.4f;
        r.refresh();
        save (dir, "compact_env2.png", r.ui);
        r.tap (r.ui.compact().full);
        save (dir, "full_screen.png", r.ui);
        ui::theme::setDetail (1);
    }
    ui::theme::setLook (1);
    std::printf ("%s: %d images, %d failed\n", g_fails ? "FAIL" : "PASS", g_images, g_fails);
    return g_fails ? 1 : 0;
}
