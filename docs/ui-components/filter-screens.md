# FilterFullScreen, FilterOutReadout, FilterKnob, FilterToggle

## What it is
The ONE Filter page (the whole tab, 2026-11-21): a top bar (close in a window, power, the state selector F1 | F2 | NOTE | VELO, the F1+F2 view toggle, model and style steppers, CLEAN toggle), the [FilterDisplay](filter-display.md), and a bottom band of blocks MAIN, TRACK, OUT and (for the formant model) FORMANT. NOTE and VELO hide all of it: the owner draws the [ScalarCurveEditor](scalar-curve-editor.md) in `bodyRect ()` under the top bar.
`FilterOutReadout`: the read-only DEST, BUS 1, BUS 2, LEVEL, PAN of the OUT block (the Mixer is their home); no touch target. `FilterKnob`: a `Knob` that dims dial and value text consistently when disabled. `FilterToggle`: a `ToggleIcon` variant for the filter page. (`FilterRoutePanel`, the OUTPUT popup, was removed.)

## Header and types
`shell-mpc/ui/filter_display.h`: `FilterFullScreen : Composite`, `FilterOutReadout : Component`, `FilterKnob : Knob`, `FilterToggle : ToggleIcon`.

## State
`FilterFullScreen` members: `close`, `power`, `view` (`UsedSelector` of four fixed cells; `setViewCells (f1Used, f2Used, noteUsed, veloUsed)`, `setView (0 .. 3)`, `viewState()`, `curveView()`), `both` (`LedToggle` F1+F2: a view only), `model`, `style` (`Stepper`, listable), `clean` (`LedToggle`), `outReadout` (`FilterOutReadout`, `state` = the five texts), `display`; MAIN: `cutoff`, `resonance`, `drive`, `mix` (`FilterKnob`); TRACK: `keytrack`, `var`, `combOffset`, `env1` (`LedToggle`); OUT: `blend` (`FilterKnob`), `routing` (`LedList`); FORMANT: `formantX`, `formantY`, `formantTrans`, `formantSpread`, and `formantRes` (label `F RESO`; it stands in the TRACK block, in the place of VAR, while the model is Formant); `groups[4]` (MAIN, TRACK, OUT frameless, FORMANT). Hidden, not dimmed: `setFormantMode (bool)` hides VAR, COMB and CLEAN and shows the FORMANT block and `formantRes`; `setCombMode (bool)` shows COMB (and hides CLEAN); `setView (2 | 3)` hides everything but the selector.
`FilterOutReadout` members: `state` (`dest`, `bus1`, `bus2`, `level`, `pan` texts), `rowName (i)`; `minSize()` 170 x 140; `touchTargets` 0.

## Size
`FilterFullScreen::minSize()` 1280 x 628. The bottom band holds twelve slots of 100 px (the knobs' minimum) and the ROUTING list.

## Touch and knob behaviour
Children keep their changes. The display's cutoff and resonance (or formant X and Y) come from the `FilterDisplay` as two changes (the second with `display.takePending`: see its page). A tap on the selector is an `Index` change (`index` = the candidate id: state + 1). A touch on a member of a group (any of the four, MODEL and STYLE in the top bar too) makes its group the active one; the readout and the hidden parts take no touch.

## Q-Link
Groups MAIN [CUTOFF, RESO, DRIVE, MIX], TRACK [KEYTRACK, ENV 1, VAR (formant RESONANCE with the Formant model), COMB (Comb only)], OUT [BLEND, ROUTING, MODEL, STYLE] (frameless: MODEL and STYLE are in the top bar), FORMANT [X, Y, TRANSPOSE, SPREAD] (Formant model only).

## Options and variants
Formant mode swaps the display for an XY pad and shows the FORMANT block (four knobs); the NOTE and VELO states hide the filter part (`curveView()`).

## Minimal example
```cpp
FilterFullScreen f;
f.place ({ 0, 110, 1280, 628 });
f.setPageMode (true);                         // nothing to close
f.display.assignIds (150, 151, 152, 153);
f.cutoff.setId (160);
f.cutoff.state.value = f.cutoff.state.modValue = cutoffNorm;
f.setFormantMode (modelIsFormant);
f.setCombMode (modelIsComb);
f.setView (0);                                // 0 F1, 1 F2, 2 NOTE, 3 VELO
Change c = f.touchDown (x, y, nowMs);
c = f.touchUp (x, y);
if (c.kind == ChangeKind::Value && c.id == 160) writeCutoff (c.value);
```

## Used on
`shell-mpc/overlay/filter_ui.h` (`FilterUi`, `kElementPage` = `kElementFilter`; ids `kDisplayCutoff` 150, `kDisplayFormant`, `kDisplayReso`, `kDisplayFormantY`; the filter curves from `synthyboy::FilterResponse`). Picture: `docs/screens/filter.jpg`.

## Limits and pitfalls
- There is ONE tab now: the state (F1, F2, NOTE, VELO) is the instance's view state; keep `view.state.selected` in step with it in `update` (`setView`). F1+F2 is a view: the knobs edit the selected filter.
- A group whose members lie apart (OUT) is frameless; the top bar starts one chip lane (16 px) below the top of the tab for the chips of MODEL and STYLE.
- A switched-off filter must be drawn dim with an OFF tag, never as a bright flat line.
- The model list has 27 entries and the style list 12: they stay steppers with a popup.
