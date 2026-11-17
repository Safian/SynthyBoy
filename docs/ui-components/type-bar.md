# TypeBar, TuneRow, BasicControls

## What it is
Three oscillator-oriented composites.
- `TypeBar`: the strip under the oscillator display with 1 to 4 slots whose content comes from a preset table (wavetable: frame, phase, RAND, MAP page; granular; sample; spectral; sub; multisample; noise).
- `TuneRow`: OCT, SEM, FIN, CRS number fields and the pitch-tracking toggle (a vertical variant for the full screen, a short variant for the sub, a noise variant).
- `BasicControls`: the four values of the Q group "Basic" of an oscillator instance, one component for the compact view and the top band of the full screen.

## Header and types
`shell-mpc/ui/composite.h`: `TypeBar`, `BarKind`, `BarItem { kind, label, weight }`, `BarPreset { name, items, numItems }`, namespace `barpresets` (`kWavetable`, `kGranular`, `kSample`, `kSpectral`, `kSub`, `kMultisample`, `kNoise`), `TuneRow`, `BasicControls`. `fillGroup (QGroup&, const TypeBar&)` is in `panel.h`.

## State
`TypeBar`: `numbers[4]`, `steppers[4]`, `details[4]`, `toggles[4]`, `toggleNumbers[4]` (slot i uses the kind of its preset item), `slot (i)`, `numSlots()`, `preset()`, `load (preset)` (slot components return to their defaults; the ids are 0: give them with `slot (i)->setId`). `BarKind`: `Frame` (read-only number), `Phase` (degrees, phase icon), `Rand`, `Number`, `Stepper`, `GrainWindow`, `UnisonDetail`, `Toggle`, `Envelope`, `ToggleNumber`, `Page`. A preset is DATA: a new preset is a new table row.
`TuneRow`: `oct`, `sem`, `fin`, `crs` (`NumberField`), `track` (`LedToggle`), `more` (`DetailButton`, hidden by default); ranges OCT -4..4, SEM -12..12, FIN -100..100, CRS -48..48 (step 0.01); `setShort`, `setVertical`, `setCrsOnly`, `setNoise` (labels PITCH, FINE, KEY TRACK), `kRowH` 56.
`BasicControls`: `selector` (`PictogramSelector`), `noiseType` (`Stepper`), `source` (`SourcePicker`), `oct` (`NumberField`), `filter`, `pan`, `level` (`Knob`), `group` (`QGroup`); `configure (OscInstance, OscKind)`, `setStacked (bool)`, `assignIds (base)` (selector, source stepper, dropdown, OCT, FILTER, noise type, PAN, LEVEL = base .. base + 7: the same call in every view gives the same ids).

## Size
`TypeBar` has no fixed minimum in the table (a strip of the owner's height); `TuneRow::minSize()` follows its variant; `BasicControls::minSize()` follows `stacked` and the instance (computed from its children).

## Touch and knob behaviour
Changes come out with the child's id: number fields `Index`, steppers `Index`, detail buttons `Request` (a `UnisonDetail` or `GrainWindow` button with a main value also `Value` / `Index` on drag), toggles `Index`, `PictogramSelector` `Index`. `turn` goes to the primary child.

## Q-Link
`BasicControls::group` is the Basic group of four: [type pictograms (or waveform / noise type), source (or OCT / FILTER), PAN, LEVEL]. `TypeBar`: the filled slots are ONE group (`fillGroup`); empty slots take no Q-Link. `TuneRow` is a block on the full screen (`OscillatorFullScreen`).

## Options and variants
`BasicControls::configure (inst, kind)` swaps pictograms, visible parts and the Q group. `TuneRow` variants as above.

## Minimal example
```cpp
BasicControls basic;
basic.configure (OscInstance::A, OscKind::Wavetable);
basic.setStacked (true);                      // the compact arrangement
basic.assignIds (200);
basic.place ({ 6, 500, 408, 220 });
basic.group.setActive (true);                 // frame + chips 1 to 4
basic.pan.state.value = pan;  basic.level.state.value = level;   // from the parameters
Change c = basic.touchDown (x, y, nowMs);
c = basic.touchUp (x, y);
if (c.id == 206 && c.kind == ChangeKind::Value) writePan (c.value);

TypeBar bar (barpresets::kWavetable);
bar.slot (0)->setId (210);                    // FRAME
```

## Used on
`OscillatorCompact::basic`, `OscillatorFullScreen::basic`, `::bar`, `::tune` (`shell-mpc/ui/screen.h`), bound in `shell-mpc/overlay/osc_ui.h` (`T::` table). Pictures: `docs/user-guide/img/osc-a.png`, `osc-type-sample.png`, `osc-sub.png`, `osc-noise.png`, `xl-osc-a.png`.

## Limits and pitfalls
- docs/MPC.md lists a "phase memory stepper" in the wavetable bar preset; `barpresets::kWavetableItems` today is FRAME, PHASE, RAND, MAP.
- `TypeBar::load` resets slot components: set ids and states AFTER it.
- `BasicControls` shows only the parts of its instance (`show (...)`); do not read hidden parts.
