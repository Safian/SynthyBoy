# Overlay control inventory and removal status (2026-11-27)

Branch `remove-overlay-controls` (from origin/develop 55853e85). Evidence level: FACT = read in code (file:line given or
named function); NOT VERIFIED = not read to the end, kept by the conservative rule. No build, no device was available.

## 1. The finding that decides everything

The generic C++ widgets of `shell-mpc/ui` are not only drawn controls. They are the **parameter/Q-Link model** of every page:

- `QGroup` holds up to four `Component*` and routes the hardware Q-Link through `Component::turn()` (qgroup.h, core.h).
  Example: `overlay/lfo_ui.h:1017` `bind (kSmoothPick, &full_.smoothSwitch, T::Toggle, ...)` - "the Q member of the LED".
- The page `*_ui.h` binds every widget to a parameter (`bind(...)`, `pullToggle/pullList/...`) and formats values/ranges for
  the native cells and the Q display from the widget state (`Knob::state`, `NumberField::state`, ...).
- The layout of the native cells (the rectangles the host skin controls are placed into, `nativeApertures()`,
  `isNativeControl()`, `visitNativeStripCells()`) comes from the widget `place()` calls.

So for the pages that are native today, the widgets are **not drawn and get no touch** (the native-gating below), but they
are LIVE as model + layout + Q adapter. Deleting a widget class means first splitting that model out of it (a
parameter-handle/Q-member type with the same `turn/state/place` contract) and re-pointing ~25 headers and ~60 host tests.
That is a refactor, not a deletion, and cannot be verified without the full build and the device. Therefore this pass
deleted no control class (rule: when reachability is unclear, keep it and list it).

## 2. Native gating per overlay page (FACT unless stated)

| Page (view kind, pages.h `make`) | Gate that hides the overlay controls | Fallback that still draws/touches C++ controls |
|---|---|---|
| Mixer (`MixerUi`, kKindMixer) | `nativeBase()` = `useNativeWidgets() && !listOpen_` (mixer_ui.h:108); `useNativeWidgets` needs `w_==1280,h_==800` and `nativeMixerWidgetsAvailable()`, which `synthyboy_mpc.cpp:440` hard-codes to `true`. draw(): `if (!nativeBase()) { ground; screen_.draw }` else only native canvas + marker covers (mixer_ui.h:276-291); `covers()` returns false under nativeBase (mixer_ui.h:300-309) | `!nativeBase()`: tests, other canvas size, and the destination ListPopup (`listOpen_`, opened only from an overlay `destButton` touch, mixer_ui.h:628-640, which cannot happen while the host owns the touch) |
| Master (`MasterUi`) | `draw()` = marker covers + `full_.draw`; `covers()` false on `full_.isNativeControl` (master_ui.h:160-170); `nativeView()` publishes Main/Tuning (master_ui.h:145) | pageTop_ != 0 (editor sub-pages); NOT VERIFIED which are reachable |
| Voice (`VoiceUi`) | `nativeVoiceSequenceControls() && pageTop_==0` (voice_ui.h:81), `full_.isNativeControl` (voice_ui.h:126) | no native sequence controls: NOT VERIFIED |
| Macros (`MacroUi`) | `setNativeControls(true)` in the ctor (macro_ui.h:78); `drawNativeFallback` only when off (macro_ui.h:221); `inst.setUiFullOpen (pageTop_!=0 \|\| !nativeControls_)` (macro_ui.h:198) | targets page (`kPageTargets`) when `nativeMacroTargetsSupported()` is false; TextEntry keyboard page (`kPageKeyboard`) is overlay-only |
| Mod (`ModUi`) | `setNativeControls(true)` (mod_ui.h:75) pushes into `ModMatrixView` and `ModSlotDetail`; `isNativeControl` per view (mod_ui.h:120-136); native board `setNativeModBoard` (mod_ui.h:737-767) | `ModSourceBoard` when native board not published; list page `kPageList` |
| Env (`EnvUi`) | page mode: `setPageMode(true)` (env_ui.h:208) and `setNativePlayEditor(active\|\|pageMode_)` -> `setNativeAll` hides ADSR/shape/time/selector/targets (env_screen.h:159-242) | the compact/expanded Play embed before `enterNativePlayEditor` (env_ui.h:500-510); NOT VERIFIED whether any production path is left |
| Lfo (`LfoUi`) | `full_.setNativePlayEditor/setNativePageControls`, `hostBackground()` (lfo_ui.h:229-260); native rects (lfo_ui.h:264-312) | list/Editor/PointMod/TextEntry pages; TODO.md:304 already lists TextEntry/PointMod as not replaced |
| Filter (`FilterUi`) | `setNativeFilterControls(frame, cells, values, indicators)` (filter_ui.h:902) with view/both/scalarView/clean/routing cells; `setNativePzToolbar` | PzCurveEditor toolbar when `pzNative` false; destination list; NOT VERIFIED per cell |
| Fx (`FxUi`) | `chainBar.nativeGround` (fx_screen.h:59); native cells fx_ui.h | TODO.md:302: "FX common GPU fallback still production" (non-canonical canvas or disabled native widget) |
| Osc (`OscPageUi`, `OscUi`) | `nativeOscDetailSupported`, `updateNativeSettings/Detail`, `osc_.full().hasNativePower()` (osc_page.h:60-160) | detail pages when unsupported; NoteMask interactive (TODO.md:9) |

TODO.md lines 9, 304-306 already say the LFO/FX/Mixer fallback removal is run by other workers (`fut: ... agy ...`). No
agy worktree exists in `git worktree list` now, but those lines are not mine to touch; this pass stays out of those pages'
code to avoid a second parallel implementation.

## 3. Widget inventory

Verdict key: DEAD = no run-time path; DUP = native control exists, C++ draw/touch unreachable in the shipped gating, but
the instance is still the Q/state model (kept, see section 1); LIVE-ONLY = only the C++ control provides the function;
KEEP-UNCLEAR = reachability not proven.

| Widget (file:lines) | Instantiated by | Reachable today | Native binding? | Verdict | Native replacement: where / which component |
|---|---|---|---|---|---|
| Knob (ui/base.h:21, 494) | every KnobPanel, EnvFullScreen, LfoFullScreen, VoiceMaster, VoiceScreen, FxSlotEditor, ModSlotDetail, OscillatorFullScreen, composites | draw/touch: only fallback/test states; Q model: yes | yes: native dial (`components/native/level`, `pan`; `native_*_controls.h` cells) | DUP + Q model | already native: dial filmstrip component |
| NumberField (base.h:521, 187) | LfoFullScreen (keyTranspose/keyTune), VoiceScreen (length, stepValue), VoiceMaster (polyphony, velocityTrack, padAmount, mpeBend), FxSlotEditor, ScalarCurveEditor (point/x/y/grid), ShapeEditor, KnobPanel, OscillatorCompact | same | yes for Voice/Master/Lfo (native fields); scalar/shape editor fields: NOT VERIFIED | DUP + Q model; ScalarCurve/Shape fields KEEP-UNCLEAR | native `name_value`/HSlider (nativ-nev-ertek-mezo.md) |
| Stepper (base.h:713, 97) | LfoFullScreen (mode, namedPicker), RateControl.division, VoiceScreen.qStepSelect, FxSlotEditor (rack, steppers), ModMatrixView filters, KnobPanel, DetailPage, ShapeEditor, ModTargetsPage, OscSpecPage | same | yes: native dropdown/selector (host list) for Lfo/Fx/Osc; `qStepSelect`, Mod filters: NOT VERIFIED | DUP + Q model, KEEP-UNCLEAR for qStepSelect/mod filters | dropdown component (Selector) with the host MenuOverlay; arrows not kept (TODO.md "phase out arrow steppers") |
| UpDownStepper (base.h:814, 106) | UnisonBox.count only (composite.h:158) <- KnobPanel.unison_ (panel.h:590) <- Osc pages | only on the OSC unison page when native detail unsupported | native OSC detail cells (`native_osc_detail.h`) | DUP, KEEP (OSC page owned by osc native work) | dropdown / name_value |
| ToggleIcon (base.h:924, 99); LedToggle (led_toggle.h:39); StripPower (mixer_strip.h:333); LedSwitchProxy (lfo_screen.h) | screen.h power; FilterFullScreen.power/both/clean; FxSlotEditor; LfoFullScreen (stereo, hostPick, smoothSwitch); VoiceMaster (outputLimiter, bendLink, padPressure, mpeOn); ModSlotDetail (auxInv, stereo, delaySync); KnobPanel.toggles_; MixerStrip.power; OscSpecPage | same | yes: native Toggle Switch / IconButton for Filter, Master, Voice, Mixer power, Lfo; Mod detail toggles: native (`isNativeControl`) | DUP + Q model (smoothSwitch is a Q member, lfo_ui.h:1017) | IconButton / Toggle Switch |
| MenuButton (base.h:1027, 54); RouteDropdown (mixer_strip.h:372) | ~40 members (BACK/CLOSE/TARGETS/ADD/UNDO/...; mixer destButton) | CLOSE/BACK etc. on overlay-only pages (curve editors, PointMod, scalar editor, Keyboard) are LIVE | native: Mixer dest, Env/Lfo targets, Filter close/expand; curve-editor BACK/UNDO/INIT/DELETE POINT/APPLY/RESET and PzCurveEditor toolbar (native only when `pzNative`): NO | DUP for native ones; LIVE-ONLY for the curve-editor command buttons | IconButton/Button component + skin action; see section 5 |
| DetailButton (base.h:1166, 104); RouteButton (1275, 37) | LfoFullScreen.targets, composite.h gear, screen.h more/route | LFO targets and Osc route native (`nativeRect(...kTargets)`, `compact_.route` still wired lfo_ui/osc_ui:1369) | yes | DUP + Q model | Button / dropdown |
| PictogramSelector (1418, 122); TypeSelector (1543, 49) | composite.h (BasicControls, WarpSlot), screen.h selector | OSC page, native when `nativeOscDetailSupported` | native OSC type selector | DUP, KEEP | native selector / RotarySelector |
| Carousel (carousel.h, 168) | LfoCompact.instance, EnvCarousel | Play embeds: `setNativeInstanceSelector(false)` in EnvUi/LfoUi ctor (env_ui.h:82, lfo_ui.h:119) means the C++ selector stays hidden-off only when native; NOT VERIFIED | native instance selector exists for Env/Lfo | DUP, KEEP-UNCLEAR | dropdown (host list) |
| UsedSelector (used_selector.h, 195) | LfoFullScreen.selector, EnvFullScreen.selector, FilterFullScreen.view/scalarView; overlay/ui_bind.h:401 | Lfo: `show(selector,!page)` (lfo_screen.h:367) so only the non-page full screen; Env: native selector when `nativeSelector_`; Filter view/scalarView: native cells (filter_ui.h:686-696) | Env, Filter yes; Lfo full screen: NOT VERIFIED | DUP; Lfo full screen KEEP-UNCLEAR | dropdown component with host list |
| ModSourceBoard (mod_board.h, 259) | ModMatrixView.board, ModulationsPage.board, PointModPage.board | native board for Mod page and OSC/LFO/ENV/Filter mods (`setNativeModBoard`); PointMod board: LIVE-ONLY (TODO.md:306) | partly | PointMod board LIVE-ONLY; others DUP | native source selector callback (TODO.md:306) |
| TextEntry (text_entry.h, 334) | MacroUi.entry_, LfoUi.entry_ | Macro rename, LFO shape name: reachable | NO (TODO.md:304, :318 host TextEditOverlay path open) | LIVE-ONLY | host TextEditOverlay; blocked, see section 5 |
| ListPopup (list.h, 262) | 8 pages (env/fx/lfo/macro/mixer/mod/filter/osc `list_`) | reachable: lists for choices drawn by plugin | partly (host MenuOverlay, AGENTS.md "Lists keep the factory behaviour") | KEEP-UNCLEAR (TODO.md:9) | host MenuOverlay |
| LedList (led_list.h, 232) | Voice, Filter routing, Fx tabs/lists, Lfo, OSC spec page | native radio lists on Voice/Master/Filter/Lfo | yes | DUP + Q model | RadioList/RadioGrid component |
| Fader, SliderBar (mixer_strip.h:16, 242) | MixerStrip | only `!nativeBase()` | yes: native strip | DUP + Q model | native fader/hslider |
| Composite (composite.h:42, 107) and UnisonBox 17, WarpSlot 70, WarpPair 49, KnobRandPair 23, SourcePicker 28, BasicControls 116 | KnobPanel (osc pages), OscillatorCompact | OSC page | native OSC cells | DUP, KEEP (OSC) | native cells |
| RateControl, ShapePicker, SyncSwitch (lfo_parts.h, 273) | LfoFullScreen | native LFO page controls | yes (lfo native) | DUP + Q model | dropdown, radio |
| MixerStrip (mixer_strip.h:409, 114) | MixerFullScreen x kStrips | see Mixer row | yes | DUP + layout | native strip |
| HBar (base.h:1087, 73) | ModTargetsPage rows, ModulationsPage rows, ModMatrixView.amountBar | read-only depth bar on overlay-only rows; NOT VERIFIED | n/a | KEEP-UNCLEAR | |
| SmoothControl | no such class in shell-mpc (grep: 0 hits) | | | NOT PRESENT | |
| QGroup (qgroup.h:17) | every screen | Q-Link routing | n/a | KEEP (hardware Q-Link logic) | |

Curve/graph/meter code that is explicitly kept (task rule): display.h, env_graph.h, lfo_display.h, filter_display.h
(FilterDisplay, not FilterFullScreen), fx_display.h, time_ruler.h, scalar_curve_editor.h canvas, shape_editor ShapeCanvas,
pz_curve_editor PzCurveCanvas, voice_grid.h, note_mask.h, spectrum_curve.h, scene/GPU helper, page detection, markers,
`native_ui_params.h` publishing, QGroup.

## 4. Removal performed

None of the control classes. See section 1: every candidate is either live as Q/state model or its reachability needs the
device. Class-level dead scan (script `/private/tmp/claude-501/overlay-controls/dead.py`: classes in ui/overlay/kit with no
reference outside their file) found only non-control items (e.g. `OscMixIndicator`, an indicator, `Panels` in fx_layout.h,
`ClipReadout/PeakMeterReadout/VoiceMeterReadout` readouts); not touched, not controls.

## 5. LIVE-ONLY-HERE (kept, with the native replacement needed)

1. TextEntry (Macro rename, LFO shape name): needs the host TextEditOverlay write-back path (TODO.md:318); no skin construction known today.
2. PointMod ModSourceBoard (lfo_ui.h:2893): native source selector callback onto the source-assignment logic (TODO.md:306).
3. Curve-editor command buttons (ScalarCurveEditor INIT/UNDO/DELETE POINT, ShapeEditor, PzCurveEditor toolbar when not `pzNative`): native Button + skin action per command; interaction of the canvas stays.
4. ListPopup lists: host MenuOverlay as is (AGENTS.md).
5. LFO full-screen UsedSelector: unclear, see table.

## 6. Needs the full build / device to verify

Everything above that says NOT VERIFIED, plus the Mixer claim that `listOpen_` is unreachable on the host (touch ownership).

## 7. Native replacement column (coordinator request)

Filled per row in the table above ("Native replacement: where / which component"). New native elements were NOT built:
the replacements exist for the DUP rows already; for the LIVE-ONLY rows no skin construction exists (TextEntry) or the
binding needs device work (PointMod board, curve-editor buttons). Per the instruction, those are not deleted and are in TODO.md.
