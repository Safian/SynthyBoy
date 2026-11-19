# VoiceScreen and VoiceGrid

## What it is
`VoiceScreen`: the Voice page: the header (LENGTH, the step selector, the value of the selected step), the ROW list at the left of the grid, the step grid, a strip with all values of the selected step, and blocks RANDOM, SCALING, TARGETS. `VoiceGrid`: the 8-step by 6-row grid (PAN, DETUNE, CUTOFF, ENV, MOD 1, MOD 2) of drag-drawable bipolar bars; the step a voice plays is lit.

## Header and types
`shell-mpc/ui/voice_screen.h`: `VoiceScreen : Composite`. `shell-mpc/ui/voice_grid.h`: `VoiceGrid : Component`.

## State
`VoiceGrid::state` (set by the owner on every update; the grid never writes it): `length` 8 (1 to 8), `activeRow` 0 (0 to 5), `selectedStep` 0 (0 to 7), `values[6][8]` (-1 to +1), `modValues[6][8]`, `voiceStepMask` (bit s = step s has a sounding voice). Public: `baseId` 100 (step id = `baseId + row * 8 + col`). `kNumRows` 6, `kNumSteps` 8. API: `stepId (row, step)`, `setValue (row, step, v)` (for the owner: writes `state`), `value (row, step)`, `gridArea()`, `columnRect (s)`, `takePending (Change&)`, static `rowName (r)`, `formatValue (row, val, out, maxLen)`.
`VoiceScreen` members: `length`, `stepValue` (`NumberField`), `qStepSelect` (`Stepper`), `rowList` (`LedList`, six rows, the Q member 2 of the header group), `grid`; RANDOM: `randPan`, `randDetune`, `randCutoff`, `randEnv` (`Knob`), `randDetune10x` (`LedToggle`); SCALING: `scaleEnv`, `scaleLfo` (`Knob`), `scaleLfoMode` (`LedList`); TARGETS: `oscA`, `oscB`, `oscC`, `oscSub`, `oscNoise` (`LedToggle`); `groups[kMaxGroups = 6]`. API: `numGroups()`, `activeGroup()`, `setActiveGroup (g)`, static `groupName (g)` (STEP, RANDOM, RAND 10x, SCALING, TARGETS, NOISE), `turnQ`, `rowBlockRect()`, `stripRect()`, `blockRect (b)`, `inner (b)`. Constants `kHeaderH` 80, `kGridH` 236, `kStripH` 40, `kRowBlockW` 252.

## Size
`VoiceGrid::minSize()` 800 x 260; `VoiceScreen::minSize()` 1280 x 628.

## Touch and knob behaviour
`VoiceGrid`: a finger down in a column reports its bar from the finger height (the id names the step: the owner selects it); a drag sets every column it passes (a fast stroke skips columns: the skipped ones get values on the line between and the owner takes them with `takePending (c)` after each `touchMove`). Change: `Value`, `id` = `stepId (row, col)`, `value` -1 to +1 (not 0 to 1), made on touch down (always) and on each move that changes the value last reported. The grid stores nothing: `state` (the values, the selected step) is unchanged by every gesture and by `turn`; the owner writes the parameter, sets `state.selectedStep` from the id and `state.values` from the parameters on update, and the next draw shows both (the bar under the finger follows the parameter the finger wrote). Private and transient: the column and the value of the last report of the stroke. A `turn` that would not change the value (end of the range) reports nothing. When a stroke has skipped columns the owner handles the changes of `takePending` first and the change of the move last, so the step the finger is on stays the selected one.
Others: knobs `Value`, number fields and lists `Index`, toggles `Index`, `qStepSelect` `Index`.

## Q-Link
Groups STEP [step (virtual), row (virtual), LENGTH, the value of the selected step], RANDOM, RAND 10x, SCALING, TARGETS, NOISE. The ROW list lies apart from the other members, so the STEP group has no frame (its chips mark it, `setFrameless`).

## Options and variants
The row is chosen with the list, not with tabs on the grid. Virtual Q targets (step, row) are the overlay's: `QRouter::kVirtual`.

## Minimal example
```cpp
VoiceScreen v;
v.place ({ 0, 110, 1280, 628 });
v.grid.baseId = 100;                          // step ids 100 .. 147
v.randPan.setId (200);
v.grid.setValue (0, 3, 0.25f);                // row PAN, step 4
Change c = v.touchMove (x, y);
Change extra;
while (v.grid.takePending (extra)) handle (extra);     // the columns a fast stroke passed
if (c.kind == ChangeKind::Value && c.id >= 100 && c.id < 148)
    writeStep ((c.id - 100) / 8, (c.id - 100) % 8, c.value * 100.0);   // -1..+1 -> -100..+100
```

## Used on
`shell-mpc/overlay/voice_ui.h` (`VoiceUi`, `kElementPage` 10): the real example of a page with a grid (`T::GridStep`), a list popup and virtual Q targets. Picture: `docs/user-guide/img/voice.png`.

## Limits and pitfalls
- The row list needs 6 x 56 px = 336 px or two columns of three.
- The header shows the value of the selected step as a number and the cell and the strip show the same text: one text for one value (`VoiceGrid::formatValue`).
- `VoiceGrid` reports the second change of a stroke only through `takePending`: forget it and the skipped columns are not written.
