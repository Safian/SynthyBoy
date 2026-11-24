// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui_test.h"
#include "../voice_grid.h"
#include "../voice_screen.h"
using namespace synthyboy_mpc::ui;
int main()
{
    VoiceGrid grid;
    grid.place ({268,84,1004,236});
    for (int step=0;step<8;++step)
    {
        const Rect knob=grid.knobRect(step);
        uit::check(knob.x==286+125*step && knob.y==120 && knob.w==90 && knob.h==90,"actual native knob aperture");
        const Change down=grid.touchDown(knob.x+45,knob.y+45,0);
        uit::check(down.id==grid.stepId(0,step) && down.value==0,"touch selects without value jump");
        const Change move=grid.touchMove(knob.x+200,knob.y+15);
        uit::check(move.id==grid.stepId(0,step) && move.value>0,"relative knob drag keeps original step");
        grid.touchUp(0,0);
        uit::check(grid.value(0,step)==0,"component stores no parameter truth");
    }
    uit::Scene scene (1280, 628);
    grid.draw (scene.builder());
    const auto image = uit::render (scene);
    bool clear = true;
    for (int step = 0; step < VoiceGrid::kNumSteps; ++step)
    {
        const Rect dial = grid.knobRect (step);
        for (int y = dial.y; y < dial.bottom(); ++y)
            for (int x = dial.x; x < dial.right(); ++x)
                clear &= image.at (x, y) == 0;
    }
    uit::check (clear, "native knob apertures never contain fallback GPU dials");
    grid.setEnabled(false);
    uit::check(grid.turn(1).kind==ChangeKind::None,"disabled wheel cannot edit");
    char text[24]; VoiceGrid::formatValue(0,-.4f,text,sizeof text);
    uit::check(std::strcmp(text,"40L")==0,"PAN retains directional format");
    VoiceScreen screen; screen.place({0,0,1280,628});screen.setNativeControls(true);screen.setNativeSequenceControls(true);
    uit::check(screen.nativeStepKnobs(),"single native knob owner");
    return uit::g_fails ? 1 : 0;
}
