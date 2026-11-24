# Mixer and Master of the bold cut: before / after pictures

Owner decision 2026-11-21 ("Bold cut", "Good, continue" after the FX pilot), steps 2 and 3 of `docs/ui-map/SIMPLIFY.md` (section "Decided (owner 2026-11-21): bold cut", F Mixer, G Master, safe list K: U8, U9, Master LINK). Pictures: `scripts/render-ui-gallery.py` / `ui_gallery_host`, state Init, 1280 x 800, 16 Q-Link knobs (XL chips), quantised to 128 colours. `before/` = develop 0473eb47 (the FX pilot merged) before any change of these two pages; `after/` = this branch.

Nothing is removed from the engine or the parameter table, and no control moved to another page: what is hidden comes back with the switch that makes it apply. Nothing sits behind a MORE button on these two pages (the default views were already small; the spec's table for both pages is "unchanged" apart from the rows below), so there is no MORE view to show.

## Mixer

| control | before | after | where |
|---|---|---|---|
| OUT button of the MASTER strip (`bindings_[9]`, no destination parameter) | greyed-out, same size as the real destination buttons, did nothing | hidden (no rectangle, no touch); the cell stays empty so BUS 1, BUS 2 and MASTER faders keep one top line (U9) | `MixerStrip::setHasDestination`, `mixer_ui.h` |
| destination names of the routing bar (FLT 1, FLT 2, MAIN FX, DIRECT, MASTER) | outlined chips that looked like buttons, no touch handler | plain labels, the lit one in the accent colour; the bar is a picture (U8, "visually distinguish") | `mixer_screen.h` `MixerRoutingBar` |
| strips: power, destination, LEVEL, PAN, B1, B2 (7 sources), power, destination, LEVEL (2 buses), power, LEVEL (MASTER) | 51 controls with the OUT button | 50 controls; Q groups unchanged (one strip = one group: LEVEL, PAN, B1, B2) | `mixer_ui.h` |

Pictures: [before](../../screens/mixer_old.jpg) / [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/mixer-master-bold-cut/after/Init_mixer.png).

## Master

| control | before | after | where |
|---|---|---|---|
| LINK (`master.bend_down_link`) | its own Q group (one member, three idle slots) | touch-only toggle inside PITCH (owner Q4), no Q slot: MAIN has 5 groups (OUTPUT, PITCH, VOICES, VOICE OPTS, GLIDE), TUNING & MPE 4 | `voice_master.h` groups, `master_ui.h` |
| BEND DOWN (`master.bend_down_range`) | always shown | hidden while LINK is on (the engine uses the BEND UP range for both directions); BEND UP then reads BEND and takes the cell | `MasterScreen::setBendLinked` |
| MPE BEND (`master.mpe.bend_range`) | always shown | hidden while MPE is off (the engine reads it only for a member channel when MPE is on) | `MasterScreen::setMpeOn` |
| BEND TO X (`master.mpe.bend_to_x`) | always shown | hidden while MPE is off (same condition) | `MasterScreen::setMpeOn` |
| XYZ TO MACROS, Y TO WHEEL, Y BIPOLAR | shown | shown always: the engine applies them to note expression also without MPE | unchanged |
| everything else (OUTPUT, LIMIT, QUALITY pair, TUNE, TRANSPOSE, BEND UP, VOICES with PAD PRESS / PAD %, VOICE OPTS, GLIDE, SCALE, TUNING, MPE) | shown | unchanged | - |
| PAD % (`shell.pad_pressure_amount`, shell setting, no Q member) | always shown next to PAD PRESS | hidden (no rectangle, no touch, not dimmed) while PAD PRESS is off; appears with it (step 0 of `docs/ui-map/env-filter-lfo-plan.md`). No Q group closes up: PAD % was never a Q member | `MasterScreen::setPadPressureOn`, `master_ui.h` |
| Q groups | 10 (6 on MAIN, 4 on TUNING & MPE) | 9 (5 + 4); the members of a group are its live controls from slot 1 (hidden ones are no member and no slot is published for them) | `MasterUi::publishQGroup` |

Controls with a binding: MAIN 25 before, 24 in the default state (LINK on), 25 with LINK off; TUNING & MPE 13 before, 11 with MPE off (the default), 13 with MPE on. Total 38 before, 35 in the default state (spec estimate: about 37).

Pictures (Init = LINK on, MPE off): MAIN [before](../../screens/master_old.jpg) / [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/mixer-master-bold-cut/after/Init_master_main.png); MAIN with LINK off [before](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/mixer-master-bold-cut/before/Init_master_link_off.png) / [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/mixer-master-bold-cut/after/Init_master_link_off.png); TUNING & MPE [before](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/mixer-master-bold-cut/before/Init_master_tuning_mpe.png) / [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/mixer-master-bold-cut/after/Init_master_tuning_mpe.png); with MPE on [before](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/mixer-master-bold-cut/before/Init_master_tuning_mpe_on.png) / [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/mixer-master-bold-cut/after/Init_master_tuning_mpe_on.png). Voice (untouched, M8 is a judgement): before (reference retired) / [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/mixer-master-bold-cut/after/Init_voice.png). PAD PRESS on (PAD % shown): [before](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/mixer-master-bold-cut/before/Init_master_pad_press.png) / [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/mixer-master-bold-cut/after/Init_master_pad_press.png). The AFTER pictures of MAIN and MAIN with LINK off were re-rendered for step 0 (PAD % hidden while PAD PRESS is off); `mastervoice_ui_host` `testMasterPadPressure`.

## Tests

`mixfx_ui_host.cpp` (`testMixerBoldCut`: 50 controls, the MASTER strip has no destination button, a touch where it was does nothing, the routing bar takes no touch, Q groups unchanged), `mastervoice_ui_host.cpp` (`testMasterBoldCut`: counts per state, hidden not dimmed, LINK is no Q member, Q groups close up, hidden controls take no touch; `testFullCoverage` walks the four states of LINK x MPE), `qslots_host.cpp` (five and four groups), `layout_lint_host` (Master states `link_off`, `link_on`, `mpe_on`, `mpe_off`: 0 findings), `coverage_host`.
