# FilterFullScreen, FilterRoutePanel, FilterKnob, FilterToggle

## What it is
The page of a filter (the whole tab): a top bar (close, power, the CURVES list F1 / F2 / F1+F2, model and style steppers with the model list button, CLEAN toggle, route button), the [FilterDisplay](filter-display.md), and a bottom band of blocks MAIN, TRACK, OUT and (for the formant model) FORMANT.
`FilterRoutePanel`: the route popup: destination list, BUS 1, BUS 2, LEVEL, PAN. `FilterKnob`: a `Knob` that dims dial and value text consistently when disabled. `FilterToggle`: a `ToggleIcon` variant for the filter page.

## Header and types
`shell-mpc/ui/filter_display.h`: `FilterFullScreen : Composite`, `FilterRoutePanel : Composite`, `FilterKnob : Knob`, `FilterToggle : ToggleIcon`.

## State
`FilterFullScreen` members: `close`, `power`, `instances` (`LedList` F1, F2, F1+F2: which curves the display draws and which filter the knobs edit), `model`, `style` (`Stepper`, listable), `modelList` (`MenuButton`, `Down`), `clean` (`LedToggle`), `route` (`RouteButton`), `display`; MAIN: `cutoff`, `resonance`, `drive`, `mix` (`FilterKnob`); TRACK: `keytrack`, `var`, `combOffset`, `env1` (`LedToggle`); OUT: `level`, `pan`, `blend`, `routing` (`LedList`, three ways of connecting the two filters); FORMANT: `formantX`, `formantY`, `formantTrans`, `formantRes`, `formantSpread`; `groups[4]` (`groupName`: MAIN, TRACK, OUT, FORMANT). API: `setPageMode (bool)`, `setFormantMode (bool)`, `formantMode()`, `pageMode()`, `displayRect()`, `block (i)`, `numGroups()`, `activeGroup()`, `setActiveGroup (i)`, `turnQ`. Constants `kTopH` 56, `kBandH` 214, `kBlockTitleH` 34, `kRoutingW` 150.
`FilterRoutePanel` members: `destination` (`LedList`: Effects only, Direct out, None), `bus1`, `bus2`, `level`, `pan` (`Knob`), `close`, `group` (destination, bus1, bus2, level); `setPopup (popup, parent)`, `popup()`, `setBackdropId (id)`; `minSize()` 380 x 220.

## Size
`FilterFullScreen::minSize()` 1280 x 628. The bottom band holds twelve slots of 100 px (the knobs' minimum) and the ROUTING list.

## Touch and knob behaviour
Children keep their changes. The display's cutoff and resonance (or formant) come from the `FilterDisplay` (see its page for the way the second axis is read). The route popup: a tap on the backdrop is `Request` with the `setBackdropId` id. A touch on a control of a group makes it the active group.

## Q-Link
Groups MAIN [CUTOFF, RESO, DRIVE, MIX], TRACK, OUT, FORMANT (with the formant model; VAR and COMB have no meaning there). The route popup is one group.

## Options and variants
Formant mode swaps the display for an XY pad and shows the FORMANT block; popup or plain `FilterRoutePanel`.

## Minimal example
```cpp
FilterFullScreen f;
f.place ({ 0, 110, 1280, 628 });
f.setPageMode (true);                         // nothing to close
f.display.assignIds (150, 151, 152, 153);
f.cutoff.setId (160);
f.cutoff.state.value = f.cutoff.state.modValue = cutoffNorm;
f.setFormantMode (modelIsFormant);
Change c = f.touchDown (x, y, nowMs);
c = f.touchUp (x, y);
if (c.kind == ChangeKind::Value && c.id == 160) writeCutoff (c.value);
```

## Used on
`shell-mpc/overlay/filter_ui.h` (`FilterUi`, `kElementPage` = `kElementFilter`; ids `kDisplayCutoff` 150 and following; the filter curves from `synthyboy::FilterResponse`). Picture: `docs/user-guide/img/filter.png`.

## Limits and pitfalls
- The filter-1 / filter-2 tab and the CURVES list are not the same thing: the tab names the instance, the list names which curves are drawn. Keep `instances.state.index` in step with the instance in `update` (a past defect: the list showed F1 on the Filter 2 page).
- A switched-off filter must be drawn dim with an OFF tag, never as a bright flat line.
- The model list has 27 entries and the style list 12: they stay steppers with a popup.
