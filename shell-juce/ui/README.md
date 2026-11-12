# SynthyBoy GUI kit (`shell-juce/ui/`, namespace `synthyboy::ui`)

Custom JUCE GUI of the plugin: editor shell + reusable controls + panel registry. JUCE `Component`s only, no DSP; the
view-model (decisions without a display) is JUCE-free in `model/` and unit tested. Design source: `docs/ui-design/`
(`00-BRIEF`, `01-shell-layout`, `06-modulation`, `07-voice-midi-global`, `12-decisions`).

## Add a panel (one file, one registration line)

1. Create `shell-juce/ui/panels/<Name>Panel.cpp` (CMake globs `panels/*.cpp` and generates the explicit registration from the registration line; re-run `cmake -S . -B build`, nothing else to edit).
2. Copy `panels/MasterVoicingPanel.cpp` (the template) and change title, keys, controls.
3. End the file with the one registration line (page tab and order inside the page):

```cpp
SYNTHYBOY_REGISTER_PANEL (MyPanel, Page::flt, 10)   // at namespace scope of synthyboy::ui, not inside an anonymous namespace; the line starts in column 0
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

## The library and its hosts (contract for host implementers)

`shell-juce/ui` builds as the static library **`synthyboy_ui`** (target in `shell-juce/ui/CMakeLists.txt`, with `synthyboy_ui_model`, the JUCE-free
view-model). The same code renders in the JUCE plug-in, the Kick desktop and iOS app and the Akai MPC Key 61 (software framebuffer, 1024 x 600 logical):
nothing in it knows the plug-in format. The plug-in side that is NOT in the library: `shell-juce/PluginProcessor.*` (the `UiHost`),
`shell-juce/ApvtsParameterStore.*` (parameters on the APVTS) and `shell-juce/EditorAdapter.*` (the thin `juce::AudioProcessorEditor`).

### Dependencies (what a host must provide to link and compile it)

| What | Targets | Notes |
|---|---|---|
| Core + model | `synthyboy_core` (PUBLIC dependency of `synthyboy_ui`), `synthyboy_ui_model` | `synthyboy_core` carries the packages the Components include: `params.h`, `state.h`, `modulation.h`, `engine_caps.h`, `quality.h`, `readout.h`, `preferences.h`, `shell_features.h`, `content_state.h`, `user_content.h`, `user_library.h`, `library_items.h`, `offline_render.h`, `wave_convert.h`, `fx_rack.h`, `fx_types.h`, `formula.h`, `vital_plugin_import.h`, `serum_plugin_import.h`, `wavetable_edit.h`, `wavetable_render.h` and, through its PUBLIC links, `synthyboy_shapes`, `synthyboy_wtedit`, `synthyboy_filters_serum`, `synthyboy_sampleosc`, `synthyboy_granular`, `synthyboy_spectral`, `synthyboy_multisample`. `SYNTHYBOY_WITH_VITAL` may be OFF (the UI never includes the engine). The core CMake files use paths relative to their own folder: `add_subdirectory (core)` works from any parent project. |
| JUCE modules | `juce::juce_gui_basics` (brings juce_graphics, juce_events, juce_core, juce_data_structures), `juce::juce_audio_basics`, `juce::juce_audio_formats`, `juce::juce_audio_utils` | `juce_audio_utils` only for `juce::MidiKeyboardComponent` (the keys of the strip); `juce_audio_formats` for decoding user samples (`ContentIO`). **`juce_audio_processors` is not used** (it comes along with `juce_audio_utils` but no library source touches an `AudioProcessor`, a `ParameterAttachment` or an APVTS), no `juce_audio_plugin_client`, no CLAP, no `juce_opengl`, no `juce_gui_extra` API. |
| JUCE version | 7.0.12 (Kick) and 8.0.x (plug-in) | The library is compiled against both (UILIB-REPORT.md). Fonts and text width go through `JuceCompat.h`: new code that touches a font or another API that moved between 7 and 8 goes there too. |
| Threads | `Threads::Threads` | the content decoders |

A host adds `add_subdirectory (shell-juce/ui)` after `synthyboy_core` and the `juce::` targets exist and links `synthyboy_ui`. Panels register through
**explicit registration**: CMake generates `PanelRegistration.cpp` (in the build directory) with one call per `SYNTHYBOY_REGISTER_PANEL` line of `panels/*.cpp`;
`PanelRegistry::instance ()` calls it once. No static initialiser is involved, so a static library, a dylib or a different link order cannot lose a panel
(`uph_panels_registered_explicitly_and_layout_at_the_mpc_size` checks the registry against the sources).

### What a host implements

1. **`UiHost`** (`UiHost.h`). Five pure virtuals besides the parameters: `engineCaps`, `keyboardState`, `abCompare`, `stepPreset`, `presetName`; and
   `parameterStore ()`. Everything else has a default that says "this host cannot" (no MIDI learn, no locks, no content library, no file requests, no share ...): the UI
   dims or hides what the host does not offer, it never assumes. A new method of `UiHost` MUST have such a default; a host written against an older header keeps compiling.
   Defaults worth knowing: `qualityTier () = high`, `overlaysInsideEditor () = true`, `engineCaps2 () = everything`, `dragScrollsPanels () = false` (true: one finger arrives as a mouse and a drag on a background scrolls the panels; a component that uses the drag itself calls `setViewportIgnoreDragFlag (true)` in its constructor).
2. **`ParameterStore`** (`ParameterStore.h`). The only parameter access of the UI, by index into `paramTable ()` (the key and id of the entry are
   `paramTable ()[index].key / .id`). Message thread only: `contains`, `plain`, `normalised`, `toNormalised` / `fromNormalised` (the store's own conversion, the pair must round-trip),
   `beginGesture` / `endGesture` (one per drag; `setPlainComplete` wraps a single edit), `setNormalised` (writes and tells the host), and listeners (`addListener (index, l)` / `addListener (l)`,
   `parameterChanged`, `parameterGestureChanged`). A store whose values change on another thread (automation) marshals to the message thread before it calls `notifyChanged`.
   Two implementations: `ApvtsParameterStore` (plug-in; JUCE parameters, host gestures) and **`PlainParameterStore`** (in the library): an in-memory table with the core's conversions,
   `onUiChange` / `onUiGesture` callbacks to forward UI edits to the host (CLAP param events, ...) and `setFromHost` / `setAllFromHost` for changes coming from the host (no echo). Display text and
   ranges are not the store's job: the UI formats with the core (`displayText`, `valueToText`) so every host shows the same strings.
3. **The editor.** `SynthyBoyEditor (UiHost&)` is a plain `juce::Component` (default 1024 x 768, `kMinWidth` x `kMinHeight` .. `kMaxWidth` x `kMaxHeight`, layout follows the width: phone below
   ~600, tablet, wide from 800; 1024 x 600 is a tested size). Put it anywhere: `addAndMakeVisible (editor); editor.setBounds (...)`. The plug-in wraps it in `SynthyBoyEditorAdapter`.
   The editor is its own overlay root (`OverlayHost`): loupe, value bubble, toast, sheets, call-outs, the text prompt and (by default) pop-up menus are children of the editor, never of the host window.

```cpp
class MyHost final : public synthyboy::ui::UiHost            // what the Kick app writes over its CLAP plug-in instance
{
public:
    ParameterStore& parameterStore() override { return store; }            // PlainParameterStore: onUiChange -> clap param event, setFromHost <- clap value changed
    EngineCapsMask engineCaps() const noexcept override { return caps; }
    juce::MidiKeyboardState& keyboardState() override { return keyboard; }   // merge into the MIDI input of the instance
    AbCompare& abCompare() override { return ab; }
    void stepPreset (int direction) override;
    juce::String presetName() const override;
    // optional: requestFile / canRequestFile, shareFile, content(), userLibrary(), preferences(), cpuLoad(), copyScope(), qualityTier() ...
};
```

### A host without windows: the bitmap GUI of the native module

`shell-clap/bitmap/` is a fourth host of this library: the native CLAP module (`synthyboy.clap`) renders `SynthyBoyEditor` itself into a software image and offers the pixels through the CLAP extension
`com.safian.bitmap-gui/1` (`docs/BITMAP-GUI-EXTENSION.md`). Its `UiHost` is `shell-clap/bitmap/NativeUiHost.*` (a `PlainParameterStore` whose edits reach the audio thread through the lock free `UiLink`, notes and
wheels as raw MIDI, files / share / links through the host callbacks of the extension), its window is `BitmapPeer`, a `juce::ComponentPeer` that is a memory buffer (its `repaint (area)` is the dirty rectangle of a frame).
Rules for components that follow from it: paint only what changed (an idle editor must not repaint: `LiveBus::changed ()` tells a display whether the engine's read-out brought anything new, `StereoMeter` repaints when
its bars would look different), never open a native window / dialog (use `UiHost`), never use a system font by name (the module's default typeface is the embedded Roboto; `JuceCompat.h` `makeFont` already asks for the
default one), and a call-out or pop-up menu needs `UiHost::overlaysInsideEditor ()` true.

### The OS escapes

The library never opens a native window, a native file chooser or a platform dialog. What leaves the component tree goes through `UiHost`:

| Need | `UiHost` | On a host without native windows |
|---|---|---|
| Open / save a file | `requestFile` (+ `canRequestFile`) | menu entries dim; the buttons that need a file are disabled (Tuning load) |
| Share / export | `shareFile` (+ `canShareFiles`) | the sheet says "This host has no share function" |
| Open a link | `openUrl` (+ `canOpenUrl`, `aboutUrl`) | the About line of the Preferences is plain text |
| Drag a file out | `dragFileOut` | the sheet points to Share and Save as WAV |
| Text entry (preset name, bin value) | in the editor: `showTextPrompt` (a sheet with a text field and two 44 pt buttons), not an `AlertWindow` | the on-screen keyboard of the host feeds the `TextEditor` |
| Pop-up menus, call-outs (long-press sheets) | `overlaysInsideEditor ()` true: `PopupMenu::Options::withParentComponent (editor)` / `CallOutBox` with the editor as parent | menus are components of the editor and need no `ComponentPeer`; the plug-in answers false and keeps desktop level menus |
| Tool tips | `juce::TooltipWindow` with the editor as parent | child component; hover-less hosts never show it |
| Quality tier | `qualityTier ()` | the effect views of a masked effect say so |

No `juce_opengl`: every component paints with `juce::Graphics` and renders in the software renderer (the tests render headless without a window).
Components that need a window peer: **none** when `overlaysInsideEditor ()` is true; the editor itself needs to be in a tree that has a peer or a software-rendered root (the screenshot tests use
`createComponentSnapshot` on an editor that is not on the desktop). Mouse input: touch, mouse and the two-finger gestures (pinch, pan) arrive as `juce::MouseEvent` / `mouseMagnify` of the host's peer.

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
`$SYNTHYBOY_UI_SCREENSHOT_DIR`; phone, tablet and the 1024 x 600 of the Akai MPC: layout, 44 pt targets and "every control of every panel fits and does not overlap" are checked at all of them),
`synthyboy_test_ui_plainhost` (the library alone: a real `SynthyBoyEditor` on a host that is no `AudioProcessor`, with a `PlainParameterStore`: parameter edits both ways, gestures, undo, external refresh, overlays inside an embedded editor, explicit panel registration). All run in `ctest`. New panels are covered by the registry contract test without any test code.
