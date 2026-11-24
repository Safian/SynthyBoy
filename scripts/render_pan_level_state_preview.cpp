// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "shell-mpc/ui/tests/ui_test.h"

using namespace uit;
using namespace synthyboy_mpc::ui;

int main (int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf (stderr, "Usage: render_pan_level_state_preview OUTPUT.png\n");
        return 1;
    }

    // 48 px compact variant: two columns (Active / Inactive), two rows (PAN bipolar, LEVEL unipolar)
    Scene scene (452, 156, kBackgroundRgba);
    Pen pen (scene.builder(), true);
    const auto& look = theme::L();

    for (int col = 0; col < 2; ++col)
    {
        const int x = 16 + col * 218;
        const bool powered = col == 0;
        pen.textCentered ({ x, 4, 202, 24 }, powered ? "Bekapcsolva" : "Kikapcsolva",
                          look.fLabel, pen.c (look.text, 255), look.fMin);
        for (int row = 0; row < 2; ++row)
        {
            const bool pan = row == 0;
            Knob knob (pan ? "PAN" : "LEVEL");
            knob.state.compact = true;
            knob.state.side = true;
            knob.state.bipolar = pan;
            knob.state.value = knob.state.modValue = 0.5;
            knob.state.dialMax = 40;
            knob.state.status = powered ? Knob::State::Status::Active : Knob::State::Status::Off;
            knob.setEnabled (powered);
            setText (knob.state.valueText, sizeof knob.state.valueText, pan ? "C" : "50.0");
            knob.place ({ x, 32 + row * 58, 202, 48 });
            knob.draw (scene.builder());
        }
    }

    return writePng (argv[1], render (scene)) ? 0 : 1;
}
