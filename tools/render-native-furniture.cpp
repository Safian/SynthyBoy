// SynthyBoy - render the factory static page layers with the production CPU reference.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <memory>
#include <string>
#include <type_traits>
#include "../shell-mpc/ui/env_screen.h"
#include "../shell-mpc/ui/mod_matrix.h"
#include "../shell-mpc/ui/mixer_screen.h"
#include "../shell-mpc/ui/screen.h"
#include "../shell-mpc/ui/voice_master.h"
#include "../shell-mpc/ui/voice_screen.h"
#include "../shell-mpc/overlay/page_view_base.h"

using namespace synthyboy_mpc;
template<class Screen>
bool render (const std::string& path)
{
    auto scene = std::make_unique<overlay::gpu::SceneData>();
    overlay::gpu::SceneBuilder sb (*scene);
    sb.begin (overlay::gpu::kKindPlay, 0, overlay::page::kW, overlay::page::kH);
    if constexpr (std::is_void_v<Screen>)
    {
        overlay::page::ground (sb);
    }
    else
    {
        constexpr ui::Rect tab {0, overlay::page::kTabY, overlay::page::kW, overlay::page::kTabH};
        Screen screen;
        constexpr bool oscillator = std::is_same_v<Screen, ui::OscillatorFullScreen>;
        if constexpr (oscillator)
        {
            screen.setNativeInstanceSelector (true);
            screen.setNativeRouting (true);
        }
        if constexpr (std::is_same_v<Screen, ui::EnvFullScreen>)
        {
            screen.setPageMode (true);
            screen.setNativeAll (true);
        }
        screen.place (tab);
        if constexpr (oscillator)
            ui::drawOscillatorPageGround (sb, tab, tab);
        else
            overlay::page::ground (sb);
        screen.drawStaticFurniture (sb);
    }
    std::vector<uint32_t> pixels;
    overlay::renderScene (*scene, pixels);
    FILE* output = std::fopen (path.c_str(), "wb");
    if (!output)
        return false;
    std::fprintf (output, "P6\n%d %d\n255\n", overlay::page::kW, overlay::page::kTabH);
    for (int y = overlay::page::kTabY; y < overlay::page::kTabY + overlay::page::kTabH; ++y)
        for (int x = 0; x < overlay::page::kW; ++x)
        {
            const uint32_t pixel = pixels[y * overlay::page::kW + x];
            const unsigned char rgb[] = {static_cast<unsigned char>(pixel >> 16), static_cast<unsigned char>(pixel >> 8), static_cast<unsigned char>(pixel)};
            std::fwrite (rgb, 1, sizeof rgb, output);
        }
    const bool ok = !std::ferror (output);
    return std::fclose (output) == 0 && ok;
}

int main (int argc, char** argv)
{
    if (argc != 2)
        return 2;
    const std::string dir = argv[1];
    return render<ui::MasterScreen> (dir + "/master.ppm")
        && render<ui::VoiceScreen> (dir + "/voice.ppm")
        && render<ui::OscillatorFullScreen> (dir + "/osc.ppm")
        && render<ui::ModMatrixView> (dir + "/mod.ppm")
        && render<ui::EnvFullScreen> (dir + "/env.ppm")
        && render<ui::MixerFullScreen> (dir + "/mixer.ppm")
        && render<void> (dir + "/lfo.ppm") ? 0 : 1;
}
