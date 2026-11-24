// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cstdio>
#include "shell-mpc/ui/tests/ui_test.h"

using namespace uit;
using namespace synthyboy_mpc::ui;

int main (int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf (stderr, "Usage: render_voices_preview OUTPUT.png\n");
        return 1;
    }

    Scene scene (640, 124, kBackgroundRgba);
    Pen pen (scene.builder(), true, false);
    const auto& look = theme::L();
    pen.textLeft ({ 12, 9, 616, 24 }, "VOICES | UpDownStepper | 48 px", look.fLabel, pen.c (look.text, 255), look.fMin);

    const int values[] = { 1, 7, 16 };
    const char* captions[] = { "Only increase", "Both directions", "Only decrease" };
    for (int i = 0; i < 3; ++i)
    {
        UpDownStepper stepper;
        stepper.compact = true;
        stepper.setCaption ("VOICES");
        stepper.state.min = 1;
        stepper.state.max = 16;
        stepper.state.value = values[i];
        const Size size = stepper.minSize();
        const int x = 12 + i * (size.w + 20);
        stepper.place ({ x, 42, size.w, size.h });
        stepper.draw (scene.builder());
        pen.textCentered ({ x, 95, size.w, 19 }, captions[i], look.fSmall, pen.c (look.textDim, 255), look.fMin);
    }

    return writePng (argv[1], render (scene)) ? 0 : 1;
}
