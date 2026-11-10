# UILIB - the SynthyBoy editor as a host independent library

Branch `ui-lib` (cut from develop 4a5ebaf). Host contract for implementers: `shell-juce/ui/README.md` ("The library and its hosts"). Assumptions A460 .. A472 and
question 107: `13-new-keys.md` section 4.44, `14-morning-questions.md`.

## Result

| Deliverable | State |
|---|---|
| (a) target `synthyboy_ui` with explicit panel registration; plug-in and every UI test relinked | done. `shell-juce/ui/CMakeLists.txt` generates `PanelRegistration.cpp` from the 66 `SYNTHYBOY_REGISTER_PANEL` lines (a new panel is still one file); no static initialiser, `PanelRegistry::instance ()` calls `registerAllPanels ()`. |
| (b) parameter-access interface, APVTS adapter, non-APVTS test with a real `SynthyBoyEditor` | done: `ParameterStore` + `PlainParameterStore` (library), `ApvtsParameterStore` (plug-in side), `synthyboy_test_ui_plainhost` |
| (c) JUCE 7.0.12 / 8 | compile proof against the 7.0.12 headers (see below); runtime on 7.0.12 NOT proven (stated risk) |
| (d) 1024 x 600 | in the screenshot test (`ui-screenshots/mpc-1024x600.png`), in the layout test and in a new per-panel fit / overlap test at phone, tablet and 1024 x 600 |
| (e) README contract, this report, assumptions | done |
| (f) gates | full build: no compiler warning (`cmake --build build -j8`; the only line with "warning" is GNU make's "jobserver unavailable" of the nested JUCE helper build, not a compiler message). `ctest -j6`: 93 / 93 passed. `gen-factory-presets.py --check`: up to date (112 presets). `build-novital` rebuilt with `-DSYNTHYBOY_BUILD_PLUGIN=OFF -DSYNTHYBOY_WITH_VITAL=OFF` (core CMake files were touched): builds, `check-no-vital.sh` clean for `libsynthyboy_core.a` and `libsynthyboy_static.a`, self-test ok. |

## The findings of the task, verified

1. **Static initialisers would drop panels.** Confirmed (the panels were compiled into every consumer as loose object files). Fixed as above. `Panel.h`: the macro now defines `registerPanel_<Class> (PanelRegistry&)`; the generated file calls all of them.
2. **APVTS dependency.** Confirmed: `UiHost::parameters ()`, `SynthyBoyEditor : AudioProcessorEditor + AudioProcessorParameter::Listener`, `ApvtsModMatrix`, `ParamHandle` (`RangedAudioParameter`, raw atomics, `ParameterAttachment`), plus raw-atomic reads in `Strip`, `EnvCurveView`, `ContentIO`, `Editor`. All now go through `ParameterStore` (index based, message thread, listeners per parameter or for all). `SynthyBoyEditor` is a plain `juce::Component` (`Editor.h`); `shell-juce/EditorAdapter.*` is the `AudioProcessorEditor` (size and resize limits mirrored both ways). `UiContext::apvts ()` -> `UiContext::store ()`, `ApvtsModMatrix` -> `StoreModMatrix`.
3. **`UiHost` discipline.** Kept: five pure virtuals (`engineCaps`, `keyboardState`, `abCompare`, `stepPreset`, `presetName`) plus `parameterStore ()` (which replaced `parameters ()`); the three new methods (`qualityTier`, `overlaysInsideEditor`, `dragFileOut`) have defaults. Contract documented in the README.
4. **JUCE 7.0.12 / 8.0.17.** The library uses `juce_gui_basics` (+ graphics, events, core, data_structures), `juce_audio_basics`, `juce_audio_formats` (decoding of user samples in `ContentIO`) and `juce_audio_utils` (only for `juce::MidiKeyboardComponent` in the keyboard strip; it drags `juce_audio_processors` in as a module dependency, no source of the library uses it). 7.0.12 sources were found under `BehringerKick/.3rdPartySourceCache/JUCE-7.0.12`. **Proof:** a scratch project (`/private/tmp/.../scratchpad/juce7/CMakeLists.txt`) compiles all 67 sources of `synthyboy_ui` (+ core, own engine only) against the 7.0.12 headers: after the fix it compiles without error or warning. The first run found the only differences: `juce::FontOptions` and `GlyphArrangement::getStringWidth` in `Theme.cpp`; they are in `JuceCompat.h` now (`compat::makeFont`, `compat::stringWidth`). **Not proven:** linking and running against 7.0.12: `juce_gui_basics.mm` of 7.0.12 does not compile with the macOS 27 SDK of this machine (`CGWindowListCreateImage` is obsoleted), so the JUCE 7 modules could not be built here; the scratch build therefore only compiles the library (headers), it does not link `synthyboy_test_ui_plainhost`. Kick's own JUCE build is the next proof (it links the same sources).
5. **Core and model dependencies.** `synthyboy_ui` links `synthyboy_ui_model` and `synthyboy_core` PUBLIC; the exact header list and the packages reached through core's PUBLIC links are in the README table. Found and fixed while doing the JUCE 7 scratch build: three non-test CMake files of core (`wavetable-edit`, `vital-import`, `serum-import`) used `PROJECT_SOURCE_DIR`, so core could not be added from any project but this one.
6. **Plug-in-only code, OpenGL, native windows.** No `juce_audio_plugin_client`, CLAP or `juce_opengl` anywhere in the library. Things that needed a window peer or an OS service, and what happens now:
   - `juce::AlertWindow` x2 (preset name in `LibraryMenu`, bin value in the wavetable editor): replaced by `showTextPrompt` (Overlays.*), a sheet inside the editor with a text field and two 44 pt buttons.
   - `juce::FileChooser` in the Tuning panel (load .scl / .tun): now `UiHost::requestFile` (purpose `tuning`); the button is disabled when the host cannot ask.
   - `performExternalDragDropOfFiles` (drag-out of the last note): now `UiHost::dragFileOut` (default false; the plug-in implements it).
   - `PopupMenu` (37 call sites) and `CallOutBox` (long-press sheets, module sheet, filter type picker): desktop level windows unless a parent is given. Now every site goes through `popupOptions ()` / `launchCallOut ()` (`OverlayHost.h`); with `UiHost::overlaysInsideEditor ()` true (default) they are children of the editor, with the plug-in's false they stay as before.
   - `TooltipWindow` with the editor as parent: a child component already.
   - `getTopLevelComponent ()` was used for the loupe, value bubble, toast, wavetable editor sheet and drop targets: in an embedded editor that is the whole host window (the sheets would have covered Kick's window). All use the editor as overlay root now (`OverlayHost`).
   - The OS escapes of the kit are now exactly `requestFile`, `shareFile`, `dragFileOut` (all with a default "cannot").
7. **Layout at 1024 x 600.** Added to `uir_editor_layout_touch_targets_and_modes` and to `uir_preset_strip_and_screenshots` (`ui-screenshots/mpc-1024x600.png`). A new test, `uir_every_panel_control_fits_at_the_phone_tablet_and_mpc_sizes`, checks every control of every panel (not only what is scrolled into view): at least 44 pt, inside its panel, no overlap with another control. It found two real defects (present at every size, not only 600): `ModCurvePanel` ("Slot Curve & Remap") and `ModVelNotePanel` ("Velocity & Note Curves") reserved one row for their control flow, so knobs, toggles and the More button sat 130 to 170 pt below the panel. Both now measure the flow (`preferredHeight`). The other layouts did not regress (same tests, same checks). The 1024 x 600 shell keeps the two-row keyboard strip (130 pt of 600, panels scroll in ~390 pt): question 107.

## Bugs found on the way (fixed, each with a test or a changed screenshot)

- The tier test of five effect views (`#if SYNTHYBOY_DEFAULT_TIER == low`) compared two undefined identifiers: true whenever the tier was defined, so on the high tier Bode, Flanger, Convolve, Hyper/Dim and Reverb carried the "BYPASSED BY LOW TIER" label (A467). Now `UiHost::qualityTier ()`.
- Redo never worked: the writes of an undo ended gestures that the editor recorded as new undo steps and cleared the redo stack (A468). Covered by `uph_real_editor_on_a_non_apvts_host` (undo, redo).
- The two panel heights above.
- Teardown: a call-out that JUCE deletes on a later message used to reach the store through its `ParamHandle` after the host was gone (crash at shutdown in the first version of the plain-host test). `ParameterStore::lifetime ()` lets a handle detach safely, and the editor dismisses its open call-outs when it is destroyed.

## Tests

- `synthyboy_test_ui_plainhost` (new, links only `synthyboy_ui`, no `PluginProcessor`, no adapter, no APVTS): store contract (listeners, host vs UI changes, self-removal, whole-patch helpers); a real editor on `MemoryHost` (drag = one gesture forwarded to the host callbacks, undo / redo through the store, host change reaches the control without echo, reset, a modulation slot changed from outside is seen by the assign controller, A/B); explicit panel registration (66 panels, ordered, once) and layout at 1024 x 600, 1024 x 768, 390 x 844; overlays of an editor embedded in a foreign window (call-out and text prompt are children of the editor and no desktop component appears, toast goes to the editor, open call-out at destruction).
- `synthyboy_test_ui_render`: 1024 x 600 case and shot, the per-panel fit test.
- All other UI suites: relinked to `synthyboy_ui`, `dynamic_cast<SynthyBoyEditor*>` replaced by `innerEditor ()` (the adapter's editor), delegating test hosts forward `parameterStore ()` and `overlaysInsideEditor ()`; no expectation changed.
- Screenshots: `phone-390x844.png` and `tablet-1024x768.png` changed on purpose (tier label gone, the two panel heights), `mpc-1024x600.png` is new; the wtremap shots are untouched.

## Remaining risks

1. JUCE 7.0.12: compile-only proof (see item 4); a behaviour difference of 7.0.12 at run time (for example `PopupMenu::Options::withParentComponent`, `CallOutBox` with a parent) is possible. The first Kick build is the real test.
2. `PopupMenu` with a parent component is clipped to the editor: long menus scroll inside the editor instead of reaching over its border (that is why the plug-in keeps desktop menus). On the MPC at 600 pt a very long menu (the 1246 modulation destinations) depends on JUCE's scroll behaviour inside the parent: not exercised by a test (menus need a message loop).
3. The software framebuffer itself (Akai MPC) was not run: the tests render with JUCE's software renderer headless; `createComponentSnapshot` is the same path. Touch events (two-finger gestures) come from the host's peer.
4. `juce_audio_utils` is a module dependency only for `MidiKeyboardComponent`; a host that wants to drop `juce_audio_processors` from its JUCE build would have to replace the keyboard first.
5. The `PlainParameterStore` conversion is the core's; a host whose own parameters use another curve must implement `ParameterStore` itself (the interface allows it, the pair `toNormalised / fromNormalised` only has to round-trip).
6. The Apple linker's duplicate-library notice is switched off for the whole build (`add_link_options`), not only for the new targets.
