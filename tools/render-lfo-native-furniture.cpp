// SynthyBoy - offline native LFO furniture from the production layout.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <memory>
#include <string>
#include "../shell-mpc/ui/lfo_screen.h"
#include "../shell-mpc/overlay/page_view_base.h"
using namespace synthyboy_mpc;
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    for (int layout=1; layout<=6; ++layout) {
        auto scene=std::make_unique<overlay::gpu::SceneData>();
        overlay::gpu::SceneBuilder sb(*scene);
        sb.begin(overlay::gpu::kKindPlay,0,overlay::page::kW,overlay::page::kH);
        overlay::page::ground(sb);
        ui::LfoFullScreen screen;
        screen.setPageMode(true);
        screen.setNativePageControls(true);
        screen.configure(layout>3);
        screen.setKeyActive(layout==3 || layout==6);
        screen.place({0,overlay::page::kTabY,overlay::page::kW,overlay::page::kTabH});
        screen.setActiveGroup(-1);
        screen.drawNativeFurniture(sb);
        std::vector<uint32_t> pixels;
        overlay::renderScene(*scene,pixels);
        auto path=std::string(argv[1])+"/lfo_native_furniture_"+std::to_string(layout)+".ppm";
        FILE* out=std::fopen(path.c_str(),"wb");
        if (!out) return 3;
        std::fprintf(out,"P6\n1280 628\n255\n");
        for(int y=overlay::page::kTabY;y<overlay::page::kTabY+overlay::page::kTabH;++y)
            for(int x=0;x<1280;++x) {
                auto p=pixels[y*1280+x];
                unsigned char rgb[]={static_cast<unsigned char>(p>>16),static_cast<unsigned char>(p>>8),static_cast<unsigned char>(p)};
                std::fwrite(rgb,1,3,out);
            }
        if(std::fclose(out)) return 4;
    }
}
