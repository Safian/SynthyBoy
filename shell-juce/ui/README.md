# SynthyBoy GUI kit (`shell-juce/ui/`, namespace `synthyboy::ui`)

Custom JUCE GUI of the plugin: editor shell + reusable controls + panel registry. JUCE `Component`s only, no DSP; the
view-model (decisions without a display) is JUCE-free in `model/` and unit tested. Design source: `docs/ui-design/`
(`00-BRIEF`, `01-shell-layout`, `06-modulation`, `07-voice-midi-global`, `12-decisions`).

## Add a panel (one file, one registration line)

1. Create `shell-juce/ui/panels/<Name>Panel.cpp` (CMake globs `panels/*.cpp`; re-run `cmake -S . -B build`, nothing else to edit).
2. Copy `panels/MasterVoicingPanel.cpp` (the template) and change title, keys, controls.
3. End the file with the one registration line (page tab and order inside the page):

```cpp
SYNTHYBOY_REGISTER_PANEL (MyPanel, Page::flt, 10)
```

```cpp
class MyPanel final : public Panel
{
public:
    using Panel::Panel;
    juce::String title() const override { return "Filter 1"; }
    std::vector<std::string> keys() const override { return { "filter1.on", "filter1.cutoff", ... }; }  // primary first
    void build() override
    {
        addControl ("filter1.cutoff");          // primary: knob / toggle / choice picked from ParamInfo (max 4 next to a visual)
        addMoreControl ("filter1.drive");       // behind the panel's "More" toggle
    }
};
```

Rules the test suite enforces for every registered panel (`uir_panel_registry_contract`): every key exists in the parameter table
(never invent keys; `GAP` features use no key), `keys()` equals what `build()` created, in the same order, at most 4 primary
controls. Panels with a big visual (wavetable, filter curve, envelope) override `preferredHeight (width)` and `resized()` and call
`layoutControls (area, false)` for the controls below the visual. Wireframes and key lists per area: `docs/ui-design/02`..`08`
(section 2 = layout, section 6 = key coverage). Kick remote pages (`docs/kick-pages.json`) are generated elsewhere, not here.

Use only `ctx.theme` colours / sizes (`ctx.theme.colours.accent`, `ctx.theme.px (..)`, `ctx.theme.touch()`), never literals.

## Controls (all bound by KEY, never by index)

| Class | Use | Gestures |
|-------|-----|----------|
| `ParamKnob` | continuous key (also `setCompact` for the macro row) | vertical drag; slide sideways while dragging = fine zones x0.25 / x0.05 (value bubble shows the ladder); double-tap = reset to default (Q3); long-press 400 ms or right-click = sheet; mouse wheel |
| `ParamSlider` | continuous key, horizontal | same, horizontal drag |
| `ParamChoice` | stepped key, > 2 values | tap = option list, `<` `>` step; entries whose cap bit is missing are disabled |
| `ParamToggle` | stepped key with 0..1 (power buttons, Off/On) | tap |
| `makeControl (ctx, key)` | picks one of the above by `ParamInfo` | |

Every control: value text with unit from `ParamInfo` (`displayText`, Hz >= 1000 as kHz); dimmed to 40 % with the tooltip
`not available in this engine` when `requiredCaps()` of the key is not covered by `UiHost::engineCaps()` (still editable);
purple target ring while a modulation source is armed and the key is modulatable; `ModRing` shows assigned depth.
Long-press sheet (`Sheet.h`): exact value entry (`1.5k`, units allowed, empty + Enter = cancel), `-` / `+`, default and range,
Reset, MIDI learn (G07: arm / cancel / remove CC, see docs/ui-design/15-shell-features.md; enabled only if `UiHost::canMidiLearn()`), Assign mod (source menu arms the flow), list of the modulations
on the key with bypass and remove.

## Modulation by touch (06-modulation.md)

Tap a source chip (`ModSourceBar`, toggled by `MOD` in the top bar; `MacroRow` knobs are destinations) = armed (purple). Drag a
modulatable knob: the drag sets `modNN.amount` instead of the value; the slot is allocated by `AssignFlow` (reuse of the slot with the
same source and destination, else the first free one; source written last; centred destinations get `modNN.bipolar = 1`). Release =
armed again for the next knob, tap the chip or `Done` = leave. 64 slots full: message in the drawer, existing slots stay editable.
All access goes through `ModMatrixAccess` (`ApvtsModMatrix` on the APVTS), so the state machine is tested on a plain array.
The ring amount is drawn as a fraction of the control's normalised range (an approximation of the engine's internal scale).

## Thread safety

Everything runs on the message thread. Writes: `juce::ParameterAttachment` (`ParamHandle`) = `setValueNotifyingHost` inside
begin / end gesture for host automation; one drag = one gesture. Reads: the parameter atomics. The audio thread is never touched by
the UI except `MidiKeyboardState` (JUCE's own thread-safe note queue, merged in `processBlock`).

## Layout kit

`PanelHost` + `PageTabs`: width < 600 phone = one column, panels of the selected page; 600..999 tablet = two columns, same;
>= 1000 wide = all panels as tiles in three columns (masonry), tabs scroll to the first tile of their page. Switch is by the
editor width only (`layoutModeForWidth`). Always visible (01-shell-layout): `TopBar` (preset prev / next call
`UiHost::stepPreset`, name from `UiHost::presetName()`, A/B, MOD, menu), `MacroRow` (macro.1..8), `PageTabs`, `KeyboardStrip`
(notes into `UiHost::keyboardState()`; touch position in the key = velocity).
A/B (Q4): `AbCompare` holds two `ParamValues` snapshots in the processor; the first toggle starts B as a copy of A.

Sizes: `Theme::touch()` = 44 points minimum for every interactive control at any UI scale (menu "UI size"); the test suite measures
all visible controls at phone / tablet sizes. Dark / light palette: `Theme::dark()` / `light()`, one `SynthLookAndFeel`.

## Host glue

`UiHost` (UiHost.h) is the only thing the kit needs from the processor: APVTS, engine caps, keyboard state, A/B, preset prev / next +
name, MIDI learn, Save as Default, module locks (`canMidiLearn`, `saveAsDefault`, `initPatch`, `isModuleLocked`, `loadPatch`). `SynthyBoyProcessor` implements it (`setPresetStepHandler`, `setPresetName` for Kick's preset list).
`createEditor()` returns `SynthyBoyEditor`; `SYNTHYBOY_GENERIC_UI=1` in the environment gives the generic JUCE editor back.

## Tabbed pages and patch content

A panel can fold itself away: `Panel::hiddenInLayout()` (the `PanelHost` skips it). The LFO page uses it: `LfoTabsPanel` (L1..L10 | R1..R4,
registered like any panel; tap = show that LFO, drag a chip onto a chip of the same kind = copy all of its settings and its custom shape)
writes the choice to `UiContext::view->lfoTab`, every LFO panel hides itself unless it is the chosen one. Editor-wide view state lives in
`ViewState` (shared by all panels of one editor).

Data that is not a parameter (the custom LFO shape point lists, `content.lfo_shapes[]`) goes through `UiHost::content ()` (a copy) and
`UiHost::setContent ()` (validated with `validateContent`, handed to the engine, host state marked dirty). `LfoShapeEditor`
(`panels/LfoShapeEditor.*`) is the editor of one point list: drag / add / delete points, curve power per segment, Draw tool, snap, presets,
undo (one drag = one step); it previews with the engine's own evaluator (`core/shapes`). Gesture status of every design row:
`docs/ui-design/UI-GESTURE-AUDIT.md`.

A listener (`ParamHandle::onChange`) runs before the APVTS has updated its raw atomic: inside `onChange` read the new value with
`normalised ()`, not `plain ()` (see `choiceIndexOf` in `LfoPanel.cpp`).

## Gestures of the gap closure

Module sheet (`ModuleSheet.*`, header long-press / `...` button / header drag in `Panel`), `GestureKit.*` (Loupe, TwoFingerInput, LongPressDetector, Toast), `model/GestureModel.*` (zoom / pan, slices, two-finger tracker, warp pictures). Per-row status and file:line: `docs/ui-design/UI-GESTURE-AUDIT.md`; summary `docs/ui-design/UI-GAPS-REPORT.md`. Tests: `synthyboy_test_ui_gaps` (screenshots to `/tmp/ui-shots/gaps/`).

## Tests

`synthyboy_test_ui_model` (pure logic), `synthyboy_test_ui_lfo` (LFO page: tab chips, chip copy, dimming, shape editor gestures and undo, content round trip, the gestures of the audit; screenshots to `/tmp/ui-shots/follow/`), `synthyboy_test_ui_render` (real Components without a window or display; the processor
is the real one; writes the phone 390x844 and tablet 1024x768 screenshots to `ui-screenshots/` in the working directory or to
`$SYNTHYBOY_UI_SCREENSHOT_DIR`). Both run in `ctest`. New panels are covered by the registry contract test without any test code.
