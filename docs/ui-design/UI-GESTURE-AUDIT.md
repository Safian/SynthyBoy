# UI gesture audit: section 3 "Interactions" of docs/ui-design/02 .. 08

Method: every row of the Interactions table of 02, 03, 04, 05, 06, 07, 08 against `shell-juce/ui/` (state of this branch, line numbers are of the commit that contains this file). Status:

- **implemented** `file:line` = the gesture does what the row says (the generic kit gestures of a ParamKnob / ParamSlider / ParamChoice / ParamToggle are cited once, see K below).
- **partial** = a part is there (what is missing is written down).
- **NOT DONE** = missing, with the reason. `GAP` = the design itself marks the feature as not having an engine key or a core feature (no UI without a key: keys are never invented).
- **added** = missing before this audit, implemented in the same change (tests: `shell-juce/ui/tests/test_ui_lfo.cpp`).

K (kit, every control that is a plain key): vertical drag + fine zones `ParamControls.cpp:204`, double-tap = reset `:247` (reset at `:277`), long-press / right-click = sheet `:287`, `ParamControl::showSheet :77`, mouse wheel `:294`, choice tap / `<` `>` `:520-568`, toggle tap `:623-637`, assign-by-touch drag on a modulatable control `:221-235` (state machine `model/ModModel.cpp`, AssignFlow).

## 02 Oscillators (and mixer)

| # | Control / gesture | Status |
|---|---|---|
| 02-01 | Osc chip: tap selects the oscillator; long-press = module sheet; swipe on the view switches oscillator | NOT DONE: the three oscillators are three stacked panels (`OscPanel.cpp:117-119`), there is no chip row and no module sheet (copy / paste / init, listed as not in the kit in UI-KIT-REPORT.md). The LFO tab chips (`LfoTabsPanel`) show the pattern if the owner wants the same for the oscillators |
| 02-02 | Power dot `oscN.on`: tap, panel dims | partial: the key is a toggle behind More (`OscPanel.cpp:42`) and the header shows `[MUTED / OFF]` (`OscPanel.cpp:96`); the whole panel does not dim; the mixer strip name toggles it (02-17, added) |
| 02-03 | Type menu (non-HAVE types open with a "not available" badge) | implemented: `oscN.type` ParamChoice with cap dimming (`OscPanel.cpp:42`, K) |
| 02-04 | Wavetable `< >`; swipe on the name | partial: `<` `>` `OscWaveView.cpp:109`, `:118` (`WavetablePanel.cpp:141-142` for the table view); swipe on the name NOT DONE (the name is not a swipe area; the buttons are the alternative) |
| 02-05 | FRAME: horizontal drag on the view or the slider | implemented: `OscWaveView.cpp:243` (view drag), `frame` knob K; double-tap on the view resets `:297` |
| 02-06 | Main knob: vertical drag, long-press sheet | implemented: K |
| 02-07 | OCT / SEM segments + wheel | NOT DONE: `oscN.transpose` is one knob / sheet (`OscPanel.cpp:42`, numeric entry through the sheet is the alternative the design names); an OCT segment + SEM wheel is a new control, not part of the kit |
| 02-08 | FIN knob | implemented: `oscN.tune` K |
| 02-09 | Pitch MODE (Semitones / Harmonics / Ratio / Step) | GAP (no key) |
| 02-10 | Transpose snap keyboard | GAP (no key) |
| 02-11 | UNISON voices: drag on the number / `-` `+` | implemented as knob / sheet `-` `+` (K); the colour by CPU and the amber outline of the tier cap are NOT DONE (no CPU read-out in UiHost) |
| 02-12 | WARP+ type list (groups), `<` `>` | partial: `oscN.warp.type` / `morph.type` ParamChoice with `<` `>` and option list (K); the grouped list is NOT DONE (flat list) |
| 02-13 | Warp mode picture (the 2D view draws the warped frame live) | partial: `OscWaveView.cpp:11-40`, `:396` draw the warp of the three first warp types; the other types draw unwarped |
| 02-14 | Remap graph (pencil) | GAP |
| 02-15 | ROUTE sheet radio `oscN.destination` | implemented as ParamChoice (`OscPanel.cpp:42`) and the filter routing chip of the console (`OscWaveView.cpp:758`); the sheet with a radio group is not a separate widget (the choice list is the same) |
| 02-16 | Mix strip level / pan drag | partial: level of each of the 8 strips: **added** `OscWaveView.cpp:833-908`; pan stays on the Pan knob (`oscN.pan` K) |
| 02-17 | Mix strip header: tap = power; long-press = module sheet | tap = power **added** for O1 O2 O3 NOISE (`OscWaveView.cpp:909-925`); module sheet NOT DONE (see 02-01) |
| 02-18 | Mix strip header drag onto another strip (swap / copy) | NOT DONE: needs the module sheet's copy / swap model (`ModuleSnapshot::swapModules` exists in `model/ModModel.h:247` but no UI uses it yet); tablet-only gesture |
| 02-19 | Sample start / end marker drag with loupe | partial: marker drag `OscWaveView.cpp:200-260` with 44 pt tolerance; the loupe above the finger NOT DONE |
| 02-20 | Loop marker pair: drag, drag between moves both, double-tap between resets | partial: loop start / end markers drag `OscWaveView.cpp:200-260`; moving both and the double-tap reset NOT DONE; double-tap resets start / end `:297` |
| 02-21 | Sample zoom / pan (pinch, two-finger drag) | NOT DONE: the sample view has no zoom state; `+` `-` `fit` buttons are the alternative the design names and are not there either |
| 02-22 | Slice handle drag, long-press add / remove | NOT DONE: slice keys exist (`oscN.sample.slice_*`), the slice markers are not drawn / dragged; needs the engine's slice list (not a key) |
| 02-23 | Slice threshold line drag | NOT DONE: `oscN.sample.slice_threshold` is a knob (K); the line over the waveform is not drawn |
| 02-24 | X\|Y dot (granular / spectral) | GAP (Q6: granular / spectral stay GAP) |
| 02-25 | Window button (granular) | GAP |
| 02-26 | Spectral Lo / Hi markers, spectral filter editor | GAP |
| 02-27 | Sub waveform picker | GAP (no sub oscillator key) |
| 02-28 | Noise sample `< >` | NOT DONE: the noise has no sample choice key in `docs/params.json` (`NoisePanel.cpp:27-35` lists all `noise.*` keys); noise level / pan drag on the view `OscWaveView.cpp:617-650` |
| 02-29 | MAIN fader | implemented: console Main fader `OscWaveView.cpp:833` (`master.volume`), main knob in the macro row |
| 02-30 | Bus FX bypass button | implemented: `OscWaveView.cpp:757` / `:766` (`FX On` / `FX Byp`) |

## 03 Wavetable view and editor

| # | Control / gesture | Status |
|---|---|---|
| 03-01 | View tap cycles 2D -> 3D -> SP | implemented: `OscWaveView.cpp:292` (osc view), segmented switch `WavetablePanel.cpp:144-155` |
| 03-02 | View horizontal drag scrubs the frame | implemented: `WavetablePanel.cpp:286`, `OscWaveView.cpp:243` |
| 03-03 | 3D view two-finger tilt / pinch | partial: zoom buttons `WavetablePanel.cpp:163-164`; the two-finger gestures NOT DONE (JUCE `mouseMagnify` is available, the 3D projection has no tilt state) |
| 03-04 | EDIT button opens the editor | implemented: `WavetablePanel.cpp:157` |
| 03-05 | Wave canvas one-finger drag with the tool, loupe | partial: `WavetableEditorSheet.cpp:21-120` (`WaveCanvas`, 13 tools `:1159-1172`); loupe NOT DONE; tap tool then tap a cell is the same code path |
| 03-06 | Wave canvas two-finger pan / zoom | NOT DONE: no pinch in the canvas (`WavetableEditorSheet.cpp:21`); zoom buttons are not there either |
| 03-07 | Tool strip tap | implemented: `WavetableEditorSheet.cpp:1159-1172` |
| 03-08 | Grid field 1..256 | implemented: `WavetableEditorSheet.cpp:1174-1193` (menu of divisions) |
| 03-09 | Nudge tool: long-press then drag | partial: Nudge tool exists (`:1170`) and drags vertically; the long-press start is not needed with the tool selected |
| 03-10 | FFT bars drag, Even / Odd, Snap 0/25/50/75/100 | implemented: `SpectrumCanvas` `WavetableEditorSheet.cpp:122-345` (EVEN / ODD / SNAP buttons `:335-338`); numeric bin entry by double tap NOT DONE |
| 03-11 | Phase bars drag, Nudge toggle | partial: phase bars drag `:258-275`; the "nudge all phases" toggle NOT DONE |
| 03-12 | FFT menu applies at the last touched bin | implemented: `fftMenuBtn` `:339`, items `:289-299` |
| 03-13 | Thumbnail tap selects the frame | implemented: `ThumbnailsStrip` `WavetableEditorSheet.cpp:902` |
| 03-14 | Thumbnail long-press drag reorders | NOT DONE: `ThumbnailsStrip` has only `mouseDown`; SORT menu (`:1811+`) is the alternative |
| 03-15 | Select range toggle | implemented: `WavetableEditorSheet.cpp:1214-1235`, thumbnails `:902-922` |
| 03-16 | Copy / Paste / + / - | implemented: `:1209-1210` (copy / paste), prev / next frame buttons |
| 03-17 | REMOVE / SORT / MORPH menus | implemented: `:1211-1213`, items `:1811-1814` |
| 03-18 | Process `run` | implemented: `ProcessView` `:346-540`, `Run` buttons `:464` |
| 03-19 | Formula field + Apply + presets menu | implemented: `FormulaView` `:543-720`, presets `:672-683`, Apply `:711` |
| 03-20 | Import (file, method, Import) | implemented: `ImportSheet` `:934+`, methods `:970-981` (progress bar for FFT methods NOT DONE: the import is synchronous) |
| 03-21 | Export menu | implemented: `:1784-1789` (WAV 16 / 32, single cycle, save, embed) |
| 03-22 | Close `x` | implemented: `:1117-1120` (swipe down NOT DONE) |
| 03-23 | Undo / Redo (own stack, one entry per stroke) | implemented: `:1124-1126`, `pushUndo :1307`; two-finger tap NOT DONE |
| 03-24 | Modifier parameters (Layers) | implemented: `LayersView` `:721-840` |

## 04 Filters

| # | Control / gesture | Status |
|---|---|---|
| 04-01 | Response display drag: x = cutoff (log), y = resonance | implemented: `FilterViews.h:413-500` |
| 04-02 | Mode chip tap / double-tap cycles Response, +FFT, Phase | implemented: `FilterViews.h:141`, `:161`, `:503` |
| 04-03 | Formant: drag the dot (`formant.x`, `.y`) | implemented: `FilterViews.h:437` |
| 04-04 | Blend slider | implemented: `filterN.blend` K (label per model in `FilterPanels.cpp`) |
| 04-05 | Model / Style chips, `<` `>` | implemented: `FilterPanels.cpp:66-70`, menus `:326-354` |
| 04-06 | Type picker entry sets model + style (+ blend) | implemented: `FilterViews.h:732-770`, button `FilterPanels.cpp:75` |
| 04-07 | Power | implemented: `FilterPanels.cpp:39` |
| 04-08 | INPUTS chips (source -> filter) incl. the input rule | implemented: `FilterViews.h:561-600` (rule in `FilterModels.h`) |
| 04-09 | Routing chip | implemented: `FilterViews.h:684-720` |
| 04-10 | Key track knob | implemented: `filterN.keytrack` K |
| 04-11 | Main knobs | implemented: K |
| 04-12 | Var knob for GAP types | implemented: `filterN.var` K (cap dimming with the "not available" tooltip) |
| 04-13 | Filter header long-press = module sheet | NOT DONE: module sheet (see 02-01) |

## 05 Envelopes, LFOs, random LFOs

| # | Control / gesture | Status |
|---|---|---|
| 05-01 | Envelope node drag (time, sustain) with value bubble | implemented: `EnvCurveView.cpp:470-662` |
| 05-02 | Envelope curve dot vertical drag | implemented: `EnvCurveView.cpp:536` (+ reset by double-tap `:686`) |
| 05-03 | Envelope graph pinch (unlocked) / magnifier | **added**: `EnvCurveView.cpp:80-95` (pinch and wheel while the lock is off; locked = the graph fits); the magnifier slider is NOT DONE (pinch / wheel replace it) |
| 05-04 | Envelope tab tap | partial: the tab scrolls to the envelope panel (`EnvelopePanel.cpp:74`, `:115`), the six envelopes are separate panels |
| 05-05 | Envelope tab long-press: bypass / remove destinations | NOT DONE: the tabs are plain buttons without a long-press; the source chip of the bar has it (`ModSourceBar.cpp:62-92`) |
| 05-06 | Envelope tab drag onto a control (tablet) | NOT DONE: the alternative (tap the source chip, then drag the knob) is implemented (`ParamControls.cpp:221`) |
| 05-07 | Shape chooser `< >` | implemented: `LfoPanel.cpp:284` (`stepShape`), list K |
| 05-08 | Mode chooser | implemented: `lfoN.sync_type` ParamChoice (`LfoPanel.cpp` keys) |
| 05-09 | Rate area segments (Hz / Tempo / dotted / triplet / Key follow) | implemented: `lfoN.sync` ParamChoice; the rate / division / key-follow controls follow it: **added** `LfoPanel.cpp:386` (dimming) |
| 05-10 | Rate knob (rate or division) | implemented: two keys, dimmed by mode (above) |
| 05-11 | Phase slider (marker moves) | implemented: drag in the shape display `LfoPanel.cpp:239-253`, `lfoN.phase` K |
| 05-12 | Fade in, Delay, Smooth, Stereo knobs | implemented: K |
| 05-13 | Smooth toggle | implemented: `lfoN.smooth` K |
| 05-14 | LFO tab drag onto another tab = copy all settings | **added**: `LfoTabsPanel.cpp:31-60` (chip drag), `copyLfoSettings` `LfoShapeEditor.cpp:113`; the module-sheet Copy / Paste alternative is NOT DONE (see 02-01) |
| 05-15 | Shape display, Point tool: double-tap adds / deletes | **added**: `LfoShapeEditor.cpp:748` |
| 05-16 | Drag point, Snap | **added**: `LfoShapeEditor.cpp:696`, snap `:49` (grid 16 x 8); numeric x / y fields NOT DONE |
| 05-17 | Curve handle drag (per segment); "All curves" | per-segment drag **added**: `LfoShapeEditor.cpp:711-722`; "All curves" toggle NOT DONE; Segment shape menu NOT DONE |
| 05-18 | Steps tool (draw steps at grid size) | partial: the Draw tool paints 32 columns (`LfoShapeEditor.cpp:615`), and the Steps / Square / Random Steps presets give stepped shapes; a grid-size step tool is NOT DONE |
| 05-19 | Brush tool | NOT DONE: Draw tool paints levels, not brush shapes; GAP in the design (needs the brush catalogue of Vital) |
| 05-20 | Marquee select, proportional move | NOT DONE: single point selection only |
| 05-21 | Selected point bar: Loop here / Start here | NOT DONE: `content.lfo_shapes[].loopPoint` exists in the state, the engine uses it; the buttons are not in the editor yet (needs a loop marker drawing) |
| 05-22 | Modulate X / Y (point modulation) | NOT DONE: the point list carries `modX` / `modY` but the engine only honours global sources (WP123-REPORT.md "BLOCKED" 1); the editor does not write them |
| 05-23 | Grid fields (number entry, steppers) | NOT DONE: fixed 16 x 8 snap grid |
| 05-24 | Random LFO style chips | implemented: `rlfoN.style` ParamChoice + `<` `>` in the display (`LfoPanel.cpp:284`) |
| 05-25 | Random LFO toggles stereo / sync | implemented: K |
| 05-26 | VELO / NOTE graph, Init graph | implemented: `ModVelNotePanel.cpp:123-311` (see 06) |
| 05-27 | Undo / Redo of shape edits, one drag = one entry | **added**: `LfoShapeEditor.cpp:310`, `mouseUp :730` (an own stack; the global undo of the kit does not exist) |
| 05-28 | LFO tab chips L1..L10 / R1..R4 (2.2) | **added**: `LfoTabsPanel.cpp` |
| 05-29 | Shape presets | **added**: `LfoShapeEditor.cpp:403` (the 12 built-in shapes as point lists) |

## 06 Modulation

| # | Control / gesture | Status |
|---|---|---|
| 06-01 | Source chip tap = assign mode | implemented: `ModSourceBar.cpp:62-83`, flow `model/ModModel.cpp` |
| 06-02 | Source chip long-press = assignments sheet (bypass / remove) | implemented: `ModSourceBar.cpp:85-110` |
| 06-03 | Knob drag in assign mode creates / updates the slot | implemented: `ParamControls.cpp:221-247` |
| 06-04 | Long-press knob in normal mode = context sheet | implemented: `ParamControls.cpp:287`, `Sheet.cpp` |
| 06-05 | Depth halo badge: tap then drag a specific modulator | NOT DONE: the ring shows the count / depth (`Overlays.cpp:109-116`) but the badge is not a hit target; the sheet lists the modulators with remove / bypass (`Sheet.cpp:10-57`) |
| 06-06 | Quick slot pill BIP / ST / CRV / x | implemented: `ModMatrixPanel.cpp:70-99` |
| 06-07 | Macro knob drag / double-tap | implemented: K (macro row `Strip.cpp`) |
| 06-08 | Macro chip drag swaps macros | NOT DONE (gesture); the alternative `Ops Menu > Swap Macros` is implemented: `ModMatrixPanel.cpp:618-705` |
| 06-09 | Matrix row handle reorders slots | NOT DONE: reordering does not change the sound (all slots sum), the Move Up / Down row menu would need a row layout beyond 44 pt on a phone; `modNN` swap helper does not exist in `model/ModModel.h` |
| 06-10 | Matrix curve graph vertical drag (`modNN.power`) | implemented: `ModCurvePanel.cpp:150-180` (double-tap resets `:180`) |
| 06-11 | Matrix row Bypass / Remove | implemented: `ModMatrixPanel.cpp:33-36`, `:97-99` |

## 07 Voice, MIDI, global

| # | Control / gesture | Status |
|---|---|---|
| 07-01 | Portamento curve graph: drag curve, drag endpoint | implemented: `GlbPortamentoPanel.cpp:152-186` |
| 07-02 | MONO toggle pill | **added**: `GlbPortamentoPanel.cpp:216-240` (polyphony 1 <-> the last count) |
| 07-03 | LEGATO / ALWAYS / SCALED pills | implemented: ParamToggles of `master.legato`, `.portamento.always`, `.scaled` (`GlbPortamentoPanel.cpp` keys) |
| 07-04 | Voice count badge in the top bar | NOT DONE: needs the number of sounding voices; `UiHost` has no voice read-out |
| 07-05 | Quality tier card tap | implemented: `GlbQualityPanel.cpp:151` |
| 07-06 | Oscillator mapping bars: drag handles / bar body; double-tap to type notes | implemented: `GlbOscMapPanel.cpp:175-295` |
| 07-07 | Master scope / FFT display: tap / pinch toggles | NOT DONE: no oscilloscope in the kit yet (UI-KIT-REPORT.md "Not in this package"); the stereo meter (`Strip.h:17`) is there |
| 07-08 | Init / Utility buttons with confirm toast | partial: `GlbSettingsPanel.cpp:27-40` (the buttons act; a confirm toast is NOT DONE, the button text changes to "Reverted") |

## 08 Effects

| # | Control / gesture | Status |
|---|---|---|
| 08-01 | Chain slot chip tap selects the module | implemented: `FxChainPanel.cpp:219-232` |
| 08-02 | Slot power dot: double-tap or `[ON]` | partial: a tap on the dot toggles the effect (`FxChainPanel.cpp:233-247`, hit area 18 px, not 44); the double-tap variant NOT DONE |
| 08-03 | Slot handle: long-press and drag reorders | partial: drag reorders (`FxChainPanel.cpp:249-270`) without the long-press; `< Move Left` / `Move Right >` `:31-32` are the alternative; the yellow drop-target line is drawn |
| 08-04 | `[Byp All]` | implemented: `FxChainPanel.cpp:33`, `:64` (Opt/Alt-click variant NOT DONE) |
| 08-05 | Chorus / Delay / Dist / FX Filter curve: 1-finger drag X / Y | implemented: `FxChorusPanel.cpp:75`, `FxDelayPanel.cpp:100`, `FxDistortionPanel.cpp:119`, `FxFilterPanel.cpp:73` |
| 08-06 | Compressor 3-band bars | implemented: `FxCompressorPanel.cpp:101-125` |
| 08-07 | Distortion waveshaper vertical drag = drive | implemented: `FxDistortionPanel.cpp:119-165` |
| 08-08 | EQ nodes: drag X / Y; pinch = resonance | drag implemented: `FxEqPanel.cpp:135-178`; pinch (and wheel) **added**: `FxEqPanel.cpp:190-215` |
| 08-09 | Reverb damping nodes | implemented: `FxReverbPanel.cpp:100-142` |

## Added by this audit (tests in `shell-juce/ui/tests/test_ui_lfo.cpp`)

LFO tab chips + drag-to-copy, sync-mode dimming, the free-form shape editor (05-14 .. 05-18, 05-27 .. 05-29), mixer console strip level drag and name tap (02-16, 02-17), envelope pinch / wheel zoom while unlocked (05-03), EQ node pinch / wheel resonance (08-08), MONO pill (07-02).

## Rows left NOT DONE, grouped by reason

- **Needs the module sheet** (copy / paste / init / swap, long-press header): 02-01, 02-17 (sheet part), 02-18, 04-13, 05-14 (alternative). One component in the kit would close all of them.
- **GAP in the design / engine**: 02-09, 02-10, 02-14, 02-24 .. 02-27, 05-19 (brush), 05-22 (point modulation by per-voice sources).
- **No data in `UiHost`**: 07-04 (voice count), 07-07 (oscilloscope), 02-11 (CPU colour of the unison count).
- **New widgets, not feasible as a small change in the kit**: 02-07 (OCT / SEM), 02-21 .. 02-23 (sample zoom, slices), 03-06, 03-14 (thumbnail long-press reorder), 05-20 (marquee), 05-21 (loop point buttons: needs a marker drawing), 06-05 (halo badge target), 06-08 (macro chip drag), 06-09 (matrix reorder).
