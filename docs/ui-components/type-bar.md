# BasicControls

`BasicControls` in `shell-mpc/ui/composite.h` supplies the compact Play oscillator's four Basic Q-Link slots.

| Instance | Slot 1 | Slot 2 | Slot 3 | Slot 4 |
| --- | --- | --- | --- | --- |
| A, B, C | Oscillator type | Source picker | PAN | LEVEL |
| Sub | Waveform | OCT | PAN | LEVEL |
| Noise | Noise source | FILTER | PAN | LEVEL |

`configure (OscInstance, OscKind)` selects the visible controls and their group members. `OscillatorCompact::basic` uses `setStacked (true)` to fit the Play column. The component derives its minimum size from the visible controls and layout. PAN and LEVEL are always native host controls. Their existing `Knob` objects hold parameter state, geometry and Q-Link routing only; they are not registered as overlay children and cannot draw or receive pointer/wheel gestures. There is no alternate GPU layout or native-mode switch.

`assignIds (base)` assigns consecutive IDs to the type selector, source stepper, source dropdown, OCT, FILTER, noise source, PAN and LEVEL. Input returns the child ID. The stacked type selector requests its list, the source picker routes Q-Link turns to its stepper, and the group routes each turn to its active slot.

See [oscillator screens](osc-screens.md) for how `OscillatorCompact` and `OscillatorFullScreen` use their Basic groups.
