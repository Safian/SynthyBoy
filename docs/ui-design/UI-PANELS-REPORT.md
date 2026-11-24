# UI panels: build repair and review report

State: `cmake -S . -B build -DSYNTHYBOY_BUILD_PLUGIN=ON && cmake --build build -j8` compiles with zero errors and zero warnings; ctest 38/38 green; VST3 and CLAP built.
pluginval strictness 5 (VST3, `--skip-gui-tests`): SUCCESS. clap-validator: 44 run, 33 passed, 0 failed, 1 warning, 10 skipped.

## Fixed (root causes)
- Strip: keyboard scale overlay uses the `drawWhiteNote` / `drawBlackNote` hooks (JUCE 8 made `drawWhiteKey` final); hit test via `getNoteAndVelocityAtPosition`; own `MidiKeyboardState&`; wheel thumb colour from the theme.
- ODR clashes that corrupted memory (segfaults): two `OscWaveView` classes (the wavetable one is now `WavetableView`), two `FxFilterPanel` classes (the FLT page one is `FltFxFilterPanel`).
- `const Rectangle` misuse, `drawText` with float coordinates, `createDashedStroke` misuse, int/float and sign conversions, unused variables.
- Wavetable editor undo: the core `UndoStack` holds states (baseline first); the sheet now records baseline and post-edit states lazily, one undo step per edit or gesture. The UI test pushed "before" states, contradicting the core contract; corrected.
- Missing panels: the LFO page had none. `LfoPanel.cpp` adds lfo1..lfo10 and rlfo1..rlfo4 (shape display with phase drag, shape stepper, 4 primary knobs, the rest behind More).
- Touch targets (44 px) on every page at phone and tablet: envelope tabs, filter chips, mod curve and velocity/note bars, global settings buttons, wavetable view header and the editor tool strip now wrap into rows.
- Literal colours removed; `Palette` gained `danger`, `ok`, `onMod`.

## New test: `synthyboy_test_ui_panels` (shell-juce/ui/tests/test_ui_panels.cpp)
Every parameter key has a control (panels + macro row); 44 px targets on all pages at 390x844 and 1024x768; no literal colours in shell-juce/ui (outside Theme and tests); renders every page.

## Screenshots (/tmp/ui-shots/<area>/)
Per area osc, mix, flt, env, lfo, mod, fx, glb: `phone-390x844.png`, `tablet-1024x768.png` and `*-full.png` (whole scrolled page). Overviews: `/tmp/ui-shots/overview-phone-390x844.png`, `/tmp/ui-shots/overview-tablet-1024x768.png`. Wavetable sheet shots: `/tmp/ui-shots/wavetable/`.

## NOT DONE
- LFO tab chips (L1..L10 | R1..R4) and LFO copy by dragging titles: navigation is by scrolling the page; each LFO is its own panel.
- LFO free-form shape editing tools, point modulation, grid: engine GAP in design 05 (the display is read-only apart from the phase).
- Random LFO "modulation history" display: needs a UI-readable modulation value from the engine; a representative curve per style is drawn instead.
- LFO key-follow dimming by sync mode (design 05 section 4) not implemented.
- clap-validator warning (1) not investigated.
- The panel-vs-design review was automated (keys, <=4 primary, 44 px, colours) plus a visual check of the overview images; a line-by-line check of every design file's gestures was not done.
