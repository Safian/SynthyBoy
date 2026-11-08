# UI kit and editor shell - report (Phase 5 foundation)

Branch `worktree-agent-ab2804bf696a2f09f`, cut from develop 6c18f0e. Code: `shell-juce/ui/` (guide for panel authors: `shell-juce/ui/README.md`).

## What exists

| Part | Files (`shell-juce/ui/`) |
|------|--------------------------|
| Theme, fonts, 44 pt metrics, UI scale, one LookAndFeel, dark / light | `Theme.*` |
| View-model without JUCE: key resolution, display text with units, cap dimming, drag zones, double-tap / long-press rules, layout modes, pages | `model/ControlModel.*` |
| Assign-by-touch state machine on the modNN slots, mod snapshot, slot remove / bypass, A/B snapshots | `model/ModModel.*` |
| Parameter binding by key (ParameterAttachment, host gestures) | `ParamHandle.*` |
| Controls: knob, slider, choice, toggle, long-press sheet | `ParamControls.*`, `Sheet.*` |
| ModRing (purple depth ring, bipolar, bypass dashed), ValueBubble with the fine-zone ladder | `Overlays.*` |
| Source chips, source drawer, assign controller on the APVTS | `ModSourceBar.*`, `UiContext.*` |
| Panel base, self-registering registry (`SYNTHYBOY_REGISTER_PANEL`), PanelHost (1 / 2 / 3 columns by width), page tabs | `Panel.*`, `PanelHost.*` |
| Always-visible strip: preset prev / next + name, A/B, MOD, menu, macros 1..8, mini keyboard + oct | `Strip.*` |
| Editor, replaces the generic editor; `SYNTHYBOY_GENERIC_UI=1` brings it back | `Editor.*`, `PluginProcessor.*` |
| Example panel (template): Master / Voicing, all 20 `master.*` keys, 4 primary + More | `panels/MasterVoicingPanel.cpp` |

Processor changes: implements `UiHost` (APVTS, engine caps, `MidiKeyboardState` merged into `processBlock`, A/B snapshots, preset step handler and name). Nothing in the audio path changed except the keyboard merge.

## Decisions taken (all within the design docs)

- Q3 double-tap = reset, long-press (400 ms) or right-click = sheet; Q4 A/B = two `ParamValues` snapshots held by the processor (survive closing the editor; first toggle makes B a copy of A).
- Macros: 8 (`macro.1..8`) per Q2; `macro.5..8` dim by themselves while `kCapMacros58` is missing. 01-shell-layout section 4 still says 4 (older text).
- Touch targets: 44 pt minimum for every interactive control at every UI scale; visual inset is drawn inside the hit area.
- Mod ring amount is drawn as a fraction of the control's normalised range (the engine's internal scale of `modNN.amount` is not mirrored).
- New slot polarity: `modNN.bipolar = 1` for centred destinations (min < 0 < max), else 0 (06 section 1, "auto polarity").
- Sheet "Assign mod" arms the same touch flow (one mechanism); MIDI learn button is disabled until `UiHost::canMidiLearn()` (GAP in core).

## Not in this package (left for the panel packages / later, nothing hidden)

Undo / redo, pitch and mod wheel strips, TRANSPOSE / HOLD / KEY-SCALE in the keyboard strip, MAIN meter, oscilloscope, module sheet (copy / paste / init), help mode, source-chip swap for macros, per-slot curve sheet, matrix page (06 section 2.3). The panels of 02-08 (only Master / Voicing exists). The UI size / theme choice is not persisted.

## Verification

- `cmake -S . -B build -DSYNTHYBOY_BUILD_PLUGIN=ON && cmake --build build -j8`: builds (VST3 + CLAP + all tests); no warning from the new code (`-Wall -Wextra -Wpedantic -Wconversion -Wshadow` on the model, JUCE recommended warnings on the rest). Existing warnings: `missing field 'modX'` in `core/tests/test_rt_alloc.cpp`, `core/engine-vital/tests/test_wp123.cpp` (not touched).
- ctest: `synthyboy_test_ui_model` (12 cases, 222 checks) and `synthyboy_test_ui_render` (11 cases, headless Components on the real processor: key binding, one drag = one host gesture, fine zone, double-tap reset, toggle / choice / slider, cap dimming + tooltip, assign through real controls end to end, sheet value entry, A/B through the editor, keyboard note -> audio, 44 pt target measurement at 390x844, 844x390, 820x1180, 1024x768 and 150 % scale, panel registry contract) pass. Whole ctest: all green except `synthyboy_test_vital_audio` (`state_random_walk_saves_loads_and_renders_the_same`, core/engine-vital, untouched by this work; fails again on re-run, not yet checked against current develop).
- pluginval 1.0.4 strictness 5 on the VST3, in process, GUI tests on (Editor, Open editor whilst processing, Editor Automation): SUCCESS.
- Screenshots (not committed, 52 and 58 KB): `/private/tmp/claude-501/-Users-szabi--claude-spares-spare-9tVA0H/8f3fee3d-e42a-4b65-9da8-0e286cde0c4d/scratchpad/ui-screenshots/phone-390x844.png` (source drawer open, More expanded) and `.../tablet-1024x768.png`. The test writes them to `ui-screenshots/` of its working directory or `$SYNTHYBOY_UI_SCREENSHOT_DIR`.

## Not verified

Real touch hardware (multi-touch keyboard, long-press feel), Windows / Linux builds, DAW embedding of the editor (only pluginval), MPC.
