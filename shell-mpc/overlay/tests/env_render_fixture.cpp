// Actual EnvUi + matching native bridge state, synthetic offline fixture only.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <fstream>
#include <iomanip>
#include "../env_ui.h"
#include "demo_common.h"
using namespace synthyboy_mpc;
using namespace synthyboy_mpc::overlay;
struct EnvInstance : demo::Inst
{
    nativeui::FxCell fields[nativeui::env::kFields] {};
    int frames[nativeui::kEnvSelectorCells] {};
    bool visible = false;
    void setNativeEnvEditor(bool active,const nativeui::FxCell* cells) noexcept override
    { visible=active; if(cells) std::copy(cells,cells+nativeui::env::kFields,fields); }
    void setNativeEnvSelector(const int* cells) noexcept override
    { std::copy(cells,cells+nativeui::kEnvSelectorCells,frames); }
};
int main(int argc,char**argv)
{
    if(argc!=4) return 2;
    const bool beats=std::atoi(argv[1])!=0;
    ui::QGroup::setWideDevice(SYNTHYBOY_MPC_UI_GEN==2);
    demo::Rig<EnvUi,EnvInstance> r;
    r.ui.setHostBackground(true);
    r.inst.set("env1.attack",120);r.inst.set("env1.decay",450);r.inst.set("env1.sustain",65.f);r.inst.set("env1.release",300);
    r.inst.set("env1.attack_curve",4);r.inst.set("env1.decay_curve",-5);r.inst.set("env1.release_curve",-3);
    r.inst.set("env1.units",beats?1:0);r.inst.set("env1.legato_inverted",beats?1:0);
    r.refresh(2);
    auto scene=std::make_unique<gpu::SceneData>();gpu::SceneBuilder sb(*scene);
    sb.begin(gpu::kKindPlay,0,1280,800);r.ui.draw(sb);if(sb.overflow)return 3;
    std::vector<uint32_t> pixels;renderScene(*scene,pixels);
    std::ofstream raw(argv[2],std::ios::binary);
    for(int y=110;y<738;++y)for(int x=0;x<1280;++x){auto p=pixels[y*1280+x];char c[]{char(p>>16),char(p>>8),char(p),char(p>>24)};raw.write(c,4);}
    const int base=int(synthyboy::paramCount())+82;
    std::ofstream js(argv[3]);js<<std::setprecision(9)<<"{\"synthetic_fixture\":true,\"device_capture\":false,\"base\":"<<base<<",\"params\":{},\"virtual\":{";
    bool first=true;
    const auto emit=[&](int index,float norm,const char* text){if(!first)js<<',';first=false;js<<"\"Parameter "<<index<<"\":{\"norm\":"<<norm<<",\"display\":\""<<text<<"\"}";};
    emit(base+nativeui::kEnvView,r.inst.visible?1:0,"");
    for(int i=0;i<nativeui::env::kFields;++i)
    {
        int target=r.inst.fields[i].target;float norm=0;char text[80]{};
        if(target>=0 && i<nativeui::env::Selection){const auto&p=synthyboy::paramTable()[target];norm=synthyboy::toNormalised(p,r.inst.param(target));uibind::formatValue(p,r.inst.param(target),text,sizeof text);}
        emit(base+nativeui::kEnvFields+i,norm,text);
    }
    for(int i=0;i<nativeui::kEnvSelectorCells;++i)emit(base+nativeui::kEnvSelectorBase+i,float(r.inst.frames[i])/nativeui::kEnvSelectorLastFrame,"");
    js<<"}}";return raw&&js?0:4;
}
