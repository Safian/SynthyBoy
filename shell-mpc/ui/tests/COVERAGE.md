# UI Component Library Test Coverage Inventory

This document provides a comprehensive inventory of all component classes in the SynthyBoy MPC UI component library (`shell-mpc/ui/`).
For each component class, it records what it does in one line, its public behaviours across six core aspects, and the corresponding existing test (file and assertion message) or `not tested`.

## Overview Statistics

- **Total Component Classes**: 72
- **Library Headers**: `base.h`, `composite.h`, `core.h`, `display.h`, `env_graph.h`, `env_screen.h`, `filter_display.h`, `fx_chain_bar.h`, `fx_display.h`, `fx_gallery.h`, `fx_screen.h`, `fx_slot_editor.h`, `lfo_display.h`, `lfo_model.h`, `lfo_parts.h`, `lfo_screen.h`, `list.h`, `mixer_screen.h`, `mixer_strip.h`, `mod_board.h`, `mod_detail.h`, `mod_macros.h`, `mod_matrix.h`, `mod_targets.h`, `panel.h`, `qgroup.h`, `screen.h`, `shape_editor.h`, `theme.h`, `time_ruler.h`, `voice_grid.h`, `voice_master.h`, `voice_screen.h`
- **Core Helper Structures**: `QGroup` (`qgroup.h`), `TapGesture` (`core.h`), `TimeRuler` (`time_ruler.h`), `Pen` (`core.h`), `Cell` (`core.h`), `Rect`/`Point`/`Size`/`Change` (`core.h`)

---

## Component Inventory

### 1. `Knob` (base.h)
**Description**: Rotary control for continuous 0..1 parameter with optional modulation arc, detent, and side/small variants

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_base_host.cpp`: "knob: a drag up by 100 px from 0.2 gives { id 7, 0.7 }" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | `ui_base_host.cpp`: "knob: turn +0.1 from 0.5 gives 0.6" |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | `ui_base_host.cpp`: "knob: at the minimum size an 8 character label is drawn legibly" |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | `ui_base_host.cpp`: "knob: minimum size of the standard variant" |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_base_host.cpp`: "knob: drawing allocates nothing" |

### 2. `NumberField` (base.h)
**Description**: Discrete or continuous numerical input field with format options, icons, and modulation bar

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_base_host.cpp`: "number field: a drag up of 8 px = +1 step" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | `ui_base_host.cpp`: "number field: turn (+1) = next step" |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | `ui_base_host.cpp`: "number field: integer with sign: +2" |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_base_host.cpp`: "number field: drawing allocates nothing" |

### 3. `Stepper` (base.h)
**Description**: Horizontal step selector for discrete lists with left/right touch zones and wrap support

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_base_host.cpp`: "stepper: a tap on the right half = next" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | `ui_base_host.cpp`: "stepper: turn (+1) = next" |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | `ui_base_host.cpp`: "stepper: with wrap previous at the first item goes to the last" |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_base_host.cpp`: "stepper: drawing allocates nothing" |

### 4. `UpDownStepper` (base.h)
**Description**: Vertical increment/decrement stepper with top/bottom arrows for integer ranges

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_base_host.cpp`: "up/down: a tap on the upper half = +1" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | `ui_base_host.cpp`: "up/down: turn (+1)" |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_base_host.cpp`: "up/down: drawing allocates nothing" |

### 5. `ToggleIcon` (base.h)
**Description**: Binary on/off toggle button with icon (Power, PitchTrack, Generic) or knob-like styling

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_base_host.cpp`: "toggle: a tap switches on: { id 41, 1 }" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | `ui_base_host.cpp`: "toggle: turn (+1) when on gives nothing" |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_base_host.cpp`: "toggle: drawing allocates nothing" |

### 6. `MenuButton` (base.h)
**Description**: Push button with label and icon (Down chevron, Gear, Expand) emitting Request changes

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_base_host.cpp`: "menu button: a tap = { menu requested, id 51 }" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | `ui_base_host.cpp`: "menu button: turn gives nothing" |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_base_host.cpp`: "menu button: drawing allocates nothing" |

### 7. `HBar` (base.h)
**Description**: Horizontal drag bar for bipolar/unipolar modulation depth with live value marker

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_base_host.cpp`: "hbar: a drag to the right increases value" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | `ui_base_host.cpp`: "hbar: turn delta steps value" |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | `ui_base_host.cpp`: "hbar: value formatting with percentage" |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_base_host.cpp`: "hbar: draws inside rectangle" |

### 8. `DetailButton` (base.h)
**Description**: Composite button combining summary value display/drag with tap opening detail pages

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_base_host.cpp`: "detail button: a tap = { details requested, id 5 }" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | `ui_base_host.cpp`: "detail button: turn delta" |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | `ui_base_host.cpp`: "detail button: integer: nothing past the maximum" |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_base_host.cpp`: "detail button: draws inside rectangle" |

### 9. `RouteButton` (base.h)
**Description**: DetailButton specialized for routing targets with dual LED status indicators

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_base_host.cpp`: "route button: a tap = { route panel requested, id 61 }" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | `ui_base_host.cpp`: "route button: turn gives nothing" |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_base_host.cpp`: "route button: drawing allocates nothing" |

### 10. `PictogramSelector` (base.h)
**Description**: Icon-and-label multi-item segmented selector in horizontal row or vertical column

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_base_host.cpp`: "pictogram selector: a tap on an item selects it" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | `ui_base_host.cpp`: "pictogram selector: turn (+1) = the next enabled item" |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | `ui_base_host.cpp`: "pictogram selector: minimum size of five items in a row: five cells of the width of the longest label" |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_base_host.cpp`: "pictogram selector: draws inside rectangle" |

### 11. `Composite` (composite.h)
**Description**: Base container managing child component layout, event routing, and drawing order

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_env_host.cpp`: "touch routing to handles" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_composite_host.cpp`: "composite drawing pass" |

### 12. `TuneRow` (composite.h)
**Description**: Oscillator pitch tuning row (Octave, Semitone, Fine, Coarse, Track)

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_composite_host.cpp`: "tunerow standard: a tap on the middle of each leaf reaches it" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | `ui_composite_host.cpp`: "tune row: its minimum size" |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_composite_host.cpp`: "tune row standard: the picture fits one scene" |

### 13. `ToggleNumber` (composite.h)
**Description**: Side-by-side toggle and number field pair (e.g., velocity tracking and amount)

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_composite_host.cpp`: "typebar: slot touch test" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | not tested |

### 14. `TypeBar` (composite.h)
**Description**: Horizontal slot strip under oscillator display adapting to generator type preset

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_composite_host.cpp`: "typebar: touch slot" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | `ui_composite_host.cpp`: "typebar wavetable: minimum size" |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_composite_host.cpp`: "typebar wavetable: the picture fits one scene" |

### 15. `UnisonBox` (composite.h)
**Description**: Unison voice count UpDownStepper paired with detune/blend DetailButton

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_composite_host.cpp`: "unison box: a tap on the middle of each leaf reaches it" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | `ui_composite_host.cpp`: "unison box: its minimum size" |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_composite_host.cpp`: "unison box: the picture fits one scene" |

### 16. `WarpSlot` (composite.h)
**Description**: Single oscillator warp effect unit with menu selector and warp amount knob

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_composite_host.cpp`: "warp pair: a tap on the middle of each leaf reaches it" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | not tested |

### 17. `WarpPair` (composite.h)
**Description**: Dual warp slot container arranged side-by-side or stacked

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_composite_host.cpp`: "warp pair: a tap on the middle of each leaf reaches it" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | `ui_composite_host.cpp`: "warp pair: minimum size" |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_composite_host.cpp`: "warp pair: the picture fits one scene" |

### 18. `KnobRandPair` (composite.h)
**Description**: Continuous control knob paired with random modulation amount number field

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_composite_host.cpp`: "rand pair: a tap on the middle of each leaf reaches it" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | `ui_composite_host.cpp`: "rand pair: its minimum size" |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_composite_host.cpp`: "rand pair: the picture fits one scene" |

### 19. `SourcePicker` (composite.h)
**Description**: Sample/wavetable audio source stepper and browser dropdown pair

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_composite_host.cpp`: "source picker: a tap on the middle of each leaf reaches it" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | `ui_composite_host.cpp`: "source picker: minimum size" |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_composite_host.cpp`: "source picker: the picture fits one scene" |

### 20. `BasicControls` (composite.h)
**Description**: Primary 4-control Q-group quad for oscillator tuning, routing, pan, and level

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | not tested |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | not tested |

### 21. `RoutePanel` (composite.h)
**Description**: Destination selector popup with balance knob, send levels, and close button

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_composite_host.cpp`: "route panel: a tap on the backdrop closes" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | `ui_composite_host.cpp`: "route panel: its minimum size" |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_composite_host.cpp`: "route panel: the picture fits one scene" |

### 22. `EnvGraph` (env_graph.h)
**Description**: Interactive multi-stage envelope visualizer with draggable time/level/curve nodes

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_env_host.cpp`: "a touch on the centre of handle 1 takes that handle" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | `ui_env_host.cpp`: "time ruler labels" |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | `ui_env_host.cpp`: "eight touch targets of at least 56 x 56" |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_env_host.cpp`: "envelope graph scene fits ops" |

### 23. `EnvSelector` (env_screen.h)
**Description**: Envelopes 1..6 tab selector with active target presence indicators

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | not tested |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | not tested |

### 24. `EnvCompact` (env_screen.h)
**Description**: Compact overview panel for an envelope instance with mini graph and key parameters

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_env_host.cpp`: "compact envelope leaf touch test" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | `ui_env_host.cpp`: "compact envelope layout and leaves" |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_env_host.cpp`: "compact envelope: picture fits one scene" |

### 25. `EnvFullScreen` (env_screen.h)
**Description**: Full-screen envelope editor with main graph, time ruler, and ADSR parameter blocks

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_env_host.cpp`: "env full screen: leaf touch routing" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | `ui_env_host.cpp`: "env full screen layout" |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_env_host.cpp`: "env full screen: picture fits one scene" |

### 26. `FilterDisplay` (filter_display.h)
**Description**: Dual filter frequency response graph with interactive cutoff and resonance nodes

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | not tested |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_display_host.cpp`: "filter display curve render" |

### 27. `FilterRoutePanel` (filter_display.h)
**Description**: Filter routing panel with bus sends, output level, and pan controls

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | not tested |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | not tested |

### 28. `FilterKnob` (filter_display.h)
**Description**: Knob variant with specialized dim rendering when disabled for filter pages

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | not tested |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | not tested |

### 29. `FilterToggle` (filter_display.h)
**Description**: ToggleIcon variant with specialized dim/clean rendering when disabled

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | not tested |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | not tested |

### 30. `FilterFullScreen` (filter_display.h)
**Description**: Full-screen filter view with dual response curves, routing, and formant controls

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | not tested |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | not tested |

### 31. `FxSlotCard` (fx_chain_bar.h)
**Description**: Individual effect slot card in chain bar with bypass, move, and add actions

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | not tested |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | not tested |

### 32. `FxChainBar` (fx_chain_bar.h)
**Description**: Horizontal scrollable effect insert chain strip with drag-and-drop reordering

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | not tested |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | not tested |

### 33. `FxDisplay` (fx_display.h)
**Description**: Dynamic visual display for active effect (EQ curve, compressor transfer, delay taps)

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | not tested |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | not tested |

### 34. `FxGallery` (fx_gallery.h)
**Description**: Visual card gallery of available effect types with thumbnails and selection taps

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | not tested |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | not tested |

### 35. `FxFullScreen` (fx_screen.h)
**Description**: Complete FX tab view combining chain bar, slot editor, display, and gallery

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | not tested |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | not tested |

### 36. `FxSlotEditor` (fx_slot_editor.h)
**Description**: Parameter editing block for the currently selected effect instance

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | not tested |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | not tested |

### 37. `ShapePicker` (lfo_parts.h)
**Description**: Grid selector for predefined LFO waveforms and user shape styles

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_lfo_host.cpp`: "shape picker: a tap on a cell is its Index change" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | `ui_lfo_host.cpp`: "shape picker: turn steps" |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | `ui_lfo_host.cpp`: "shape picker: its minimum size" |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_lfo_host.cpp`: "shape picker drawing ops" |

### 38. `SyncSwitch` (lfo_parts.h)
**Description**: Three-way sync mode toggle (Hz, BPM, Key) with Triplet and Dotted modifiers

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_lfo_host.cpp`: "sync switch: dotted = BPM with DOT" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_lfo_host.cpp`: "sync switch dim in Hz mode" |

### 39. `RateControl` (lfo_parts.h)
**Description**: Composite speed controller switching between frequency knob and tempo division stepper

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_lfo_host.cpp`: "rate control: the knob in the Hz mode" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_lfo_host.cpp`: "rate control: the division stepper in the tempo mode" |

### 40. `InstanceBar` (lfo_parts.h)
**Description**: Selector strip for 10 LFO instances and 4 random modulation generators

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_lfo_host.cpp`: "instance bar: LFO 7 is the seventh cell" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | `ui_lfo_host.cpp`: "instance bar: its minimum size" |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_lfo_host.cpp`: "instance bar cell rendering" |

### 41. `LfoCompact` (lfo_screen.h)
**Description**: Compact overview widget for an LFO with real-time waveform display and rate

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_lfo_host.cpp`: "lfo compact touch leaf reaches" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | `ui_lfo_host.cpp`: "lfo compact minimum size" |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_lfo_host.cpp`: "lfo compact: picture fits one scene" |

### 42. `LfoFullScreen` (lfo_screen.h)
**Description**: Full-screen LFO page featuring live trace, shape selection, timing, and motion blocks

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_lfo_host.cpp`: "lfo full screen: tap on TIMING block makes it active" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | `ui_lfo_host.cpp`: "lfo full screen layout" |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_lfo_host.cpp`: "lfo full screen: picture fits one scene" |

### 43. `ListPopup` (list.h)
**Description**: Modal selection list popup with whole-row scrolling, scrollbar thumb, and header navigation

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_list_host.cpp`: "a tap on row 2: Index { id 9, index 2 }" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | `ui_list_host.cpp`: "turn +2: item 7, the window follows" |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | `ui_list_host.cpp`: "list with a header draws inside the parent" |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | `ui_list_host.cpp`: "the box holds five whole rows (5)" |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_list_host.cpp`: "draws inside the parent, one scene" |

### 44. `MixerRoutingBar` (mixer_screen.h)
**Description**: Top visual indicator bar showing signal routing paths from sources to buses/master

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | not tested |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | not tested |

### 45. `MixerFullScreen` (mixer_screen.h)
**Description**: Full-screen multi-channel mixer console view with 10 channel strips and routing bar

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | not tested |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | not tested |

### 46. `Fader` (mixer_strip.h)
**Description**: Vertical mixer channel volume fader with integrated peak/hold level meter and dB scale

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | not tested |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | not tested |

### 47. `SliderBar` (mixer_strip.h)
**Description**: Compact horizontal slider bar with text readout for channel pan and auxiliary send levels

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | not tested |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | not tested |

### 48. `MixerStrip` (mixer_strip.h)
**Description**: Full vertical channel strip composite (power, destination, pan, sends, fader)

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | not tested |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | not tested |

### 49. `ModSourceBoard` (mod_board.h)
**Description**: Interactive source matrix grid for quick addition of modulation routings

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | not tested |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | not tested |

### 50. `ModSlotDetail` (mod_detail.h)
**Description**: Dedicated detail editor page for single modulation slot (aux source, curve shaping, time)

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | not tested |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | not tested |

### 51. `SpectrumStrip` (mod_macros.h)
**Description**: Real-time FFT output frequency spectrum curve display with calibrated dB grid

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | not tested |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | not tested |

### 52. `MacroCard` (mod_macros.h)
**Description**: Individual macro knob card showing assigned destination parameter chips

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | not tested |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | not tested |

### 53. `MacroView` (mod_macros.h)
**Description**: Full page view with 8 performance macro cards and master output spectrum strip

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | not tested |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | not tested |

### 54. `ModRowSelector` (mod_matrix.h)
**Description**: Matrix row index number cell supporting selection tap and rotary navigation

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | not tested |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | not tested |

### 55. `ModRow` (mod_matrix.h)
**Description**: Modulation matrix connection row (selector, source, target, amount HBar, bypass, more)

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | not tested |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | not tested |

### 56. `ModMatrixView` (mod_matrix.h)
**Description**: Scrollable modulation routing matrix page with filtering and quick-add board

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | not tested |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | not tested |

### 57. `ModTargetsPage` (mod_targets.h)
**Description**: Paginated destination list showing all parameters modulated by a specific source

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_env_host.cpp`: "mod targets page leaf test" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | `ui_env_host.cpp`: "mod targets page layout" |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_env_host.cpp`: "mod targets page drawing" |

### 58. `KnobPanel` (panel.h)
**Description**: Data-driven block-and-item layout container for generator and effect parameters

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_composite_host.cpp`: "panel wavetable: leaf touch routing" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | `ui_composite_host.cpp`: "panel wavetable: minimum size" |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_composite_host.cpp`: "panel wavetable: picture fits one scene" |

### 59. `DetailPage` (panel.h)
**Description**: Generic sub-page view hosting a KnobPanel, header bar, and optional display

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_composite_host.cpp`: "detail page: tap on back button" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | `ui_composite_host.cpp`: "detail page: turnQ routes to active group" |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | `ui_composite_host.cpp`: "detail page: minimum size" |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_composite_host.cpp`: "detail page: picture fits one scene" |

### 60. `ModulationsPage` (panel.h)
**Description**: List view of all modulation sources routing into a specific control parameter

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_composite_host.cpp`: "modulations page: a tap on remove reaches it" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | `ui_composite_host.cpp`: "modulations page: its minimum size" |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_composite_host.cpp`: "modulations page: picture fits one scene" |

### 61. `OscillatorCompact` (screen.h)
**Description**: Compact overview panel for an oscillator instance (Sub, A, B, C, Noise)

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_screen_host.cpp`: "osc compact: leaf touch routing" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | `ui_screen_host.cpp`: "osc compact: minimum size" |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_screen_host.cpp`: "osc compact: picture fits one scene" |

### 62. `OscillatorFullScreen` (screen.h)
**Description**: Full-screen oscillator sound generator editor with display, tuning, and parameter blocks

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_screen_host.cpp`: "osc full screen: leaf touch routing" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | `ui_screen_host.cpp`: "osc full screen: areas identical across variants" |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_screen_host.cpp`: "osc full screen: picture fits one scene" |

### 63. `ToolSelector` (shape_editor.h)
**Description**: Tool selector bar for LFO curve editing (Point, Flat, Ramp Up, Ramp Down)

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_lfo_host.cpp`: "shape editor: tool selector tap selects tool" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | `ui_lfo_host.cpp`: "shape editor: tool selector turn steps" |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_lfo_host.cpp`: "shape editor tool selector rendering" |

### 64. `ShapeCanvas` (shape_editor.h)
**Description**: Interactive piecewise curve canvas with point insertion, dragging, and curvature bending

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_lfo_host.cpp`: "canvas, point tool: a tap on empty ground adds a point" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | `ui_lfo_host.cpp`: "canvas: plot and bounds" |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_lfo_host.cpp`: "editor page: the picture fits one scene" |

### 65. `ShapeEditorPage` (shape_editor.h)
**Description**: Full-screen custom LFO waveform editing view with canvas, grid snapping, and point coordinates

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | `ui_lfo_host.cpp`: "editor page: the fields work for an inner point" |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | `ui_lfo_host.cpp`: "editor page: the canvas is big" |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | `ui_lfo_host.cpp`: "editor page: the picture fits one scene" |

### 66. `VoiceGrid` (voice_grid.h)
**Description**: 8-step multi-parameter voice control step sequencer grid with touch drawing

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | not tested |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | not tested |

### 67. `PortamentoCurveDisplay` (voice_master.h)
**Description**: Visual display of portamento glide curvature as a function of slope setting

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | not tested |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | not tested |

### 68. `MiniKeyboardDisplay` (voice_master.h)
**Description**: Visual keyboard indicator highlighting root key, musical scale, and active notes

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | not tested |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | not tested |

### 69. `PeakMeterDisplay` (voice_master.h)
**Description**: Stereo output peak and peak-hold level meter with calibrated decibel graduation

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | not tested |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | not tested |

### 70. `VoiceMeterDisplay` (voice_master.h)
**Description**: Polyphony allocation meter visualizing active versus total allocated voices

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | not tested |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | not tested |

### 71. `MasterScreen` (voice_master.h)
**Description**: Full-screen master voice architecture, global pitch, glide, tuning, and MPE configuration page

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | not tested |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | not tested |

### 72. `VoiceScreen` (voice_screen.h)
**Description**: Full-screen voice sequencing page with 8-step VoiceGrid, random spreads, and scaling

| Public Behaviour | Implementation & Interface Behaviour | Existing Test Status |
| :--- | :--- | :--- |
| **Touch Down / Move / Up** | Gestures (tap, drag sensitivity, double-tap default, outside capture) | not tested |
| **Turn (Q-Link step)** | Hardware rotary knob delta handling (normalised or discrete steps) | not tested |
| **Wheel** | Mouse wheel input via `wheel()` and `wheelOver()` (`wheelUnit()`) | not tested |
| **Value Text** | Value/parameter readout formatting, units, precision, caption | not tested |
| **Layout / Minimum Sizes** | `minSize()`, touch target bounds, cell dimensions | not tested |
| **Draw Ops** | Rendering inside bounding box, SDF font text, scene op budget | not tested |

---

## Auxiliary & Support Library Classes

### `QGroup` (`qgroup.h`)
- **Description**: 4-slot hardware Q-Link controller group manager with active frame drawing, idle chips, and adjacency verification.
- **Public Behaviours**:
  - Slot management (`set`, `member`, `numMembers`): tested in `ui_composite_host.cpp` ("qgroup slot mapping")
  - Active state and Q index chips (`setActive`, `draw`, `drawIdle`, `drawMapped`): tested in `ui_composite_host.cpp` ("qgroup active chips")
  - Hardware knob routing (`turn(knobIndex, delta)`): tested in `ui_composite_host.cpp` ("qgroup turn routing")
  - Adjacency collision check (`check(all, n, &first)`): tested in `ui_screen_host.cpp` ("every Q group of every variant is adjacent")
  - Wide device 16-knob mode (`setWideDevice`, `wideDevice`): **not tested**

### `TimeRuler` (`time_ruler.h`)
- **Description**: Auto-zooming linear time tick mark generation and string formatter for temporal displays.
- **Public Behaviours**:
  - Step size selection (`rulerStepMs`): tested in `ui_time_ruler_host.cpp` ("1000 ms on 800 px, 80 px apart: step 100 ms")
  - Tick placement (`makeTimeRuler`): tested in `ui_time_ruler_host.cpp` ("the marks are one step apart")
  - String formatting (`formatRulerTime`): tested in `ui_time_ruler_host.cpp` ("labels 0, 500 ms, 1 s")

### `TapGesture` (`core.h`)
- **Description**: Single-finger tap, double-tap, and drag motion discriminator with deadband slop.
- **Public Behaviours**:
  - Tap vs Drag (`down`, `move`, `up` with `kSlop = 6`): tested in `ui_base_host.cpp` ("knob: a movement inside the tap square (3 px) is not a drag")
  - Double-tap detection (`kDoubleMs = 400`, `kDoubleDist = 30`): tested in `ui_base_host.cpp` ("knob: double tap = default (0.35)")
  - Gesture reset and drag breaking double tap: tested in `ui_base_host.cpp` ("knob: a drag between two taps breaks the double tap")

### `Pen` & `Cell` (`core.h`)
- **Description**: Direct scene op drawing abstraction managing line styling, gradients, SDF text, and cached cell boundaries.
- **Public Behaviours**:
  - Disabled dimming alpha scaling: tested in `ui_base_host.cpp` ("knob: disabled: drawn differently (dim)")
  - Centered/Left/Right fitted text: tested in `ui_base_host.cpp` ("knob: every text is at least 2 pixels per font pixel")
  - Chevron, gear, bell, envelope, route icon drawing: tested in `ui_base_host.cpp` ("detail button: draws inside rectangle")

---

## Coverage Test Plan (7 New Test Areas)

To achieve complete host test coverage of all untested behaviours across the library, tests are partitioned into 7 focused suites:
1. **Knobs and Values** (`ui_cov_knobs_values_host.cpp`): Wheel response on `Knob` and `HBar`, side/small knob layouts, `NumberField` formats and read-only mode, `Fader` / `SliderBar` bounds and sensitivities (the wheel step of one Q-Link step and the capture: regressions of the first findings), tick long press modulation requests.
2. **Steppers and Lists** (`ui_cov_steppers_lists_host.cpp`): `Stepper` listable middle tap, wrap logic, `UpDownStepper` clamping, `PictogramSelector` vertical layout, disabled item skip, `ShapePicker`, `ToolSelector`, `ModRowSelector`.
3. **Toggles and Buttons** (`ui_cov_toggles_buttons_host.cpp`): `ToggleIcon` knobLike styling, showState text, `FilterToggle` clean rendering, `MenuButton` expand icon, `DetailButton` value vs tap request separation, `RouteButton` LED indicators.
4. **Popups and Scroll Areas** (`ui_cov_popups_scroll_host.cpp`): `ListPopup` header back, scroll zone dragging, paging above/below thumb, backdrop dismissal, `FxGallery` card grid layout and tap, `ModSourceBoard` chips, `ShapeCanvas` point editing.
5. **Composites and Panels** (`ui_cov_composites_panels_host.cpp`): `Composite` base leaf collection and wheel routing, `BasicControls` 5 oscillator variants, `TuneRow` vertical/short/noise modes, `TypeBar` 5 presets, `UnisonBox`, `WarpPair`, `KnobPanel`, `FilterRoutePanel`, `MixerStrip`.
6. **Q Groups and Chips** (`ui_cov_qgroups_chips_host.cpp`): `QGroup` 4-slot routing, wide device (MPC XL 16-knob) mode, idle chips, mapped blocks, `drawMappedMembers`, frameless quad groups, adjacency detection.
7. **Text and Layout Helpers** (`ui_cov_layout_text_host.cpp`): `layoutRow` fixed/weighted calculations with gaps, `Rect` set operations (unite, overlaps, contains, inset), `ModInput` depth arc math, `setText` bounds, `TapGesture` timings, `sdf::fitText` and `Pen` text fitting.
