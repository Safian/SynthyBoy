# Native MixerStrip Channel Strip Component

Reusable Akai MPC host-native compound channel strip component, definitions, and offline design preview generator conforming to Auto MPC SDK (Section 6.1 Slider) and the SynthyBoy native component library architecture.

> [!NOTE]
> All preview images referenced here are offline design previews composited from component definitions and historical reference crops. They are not physical hardware captures or native firmware-verified renders.

## Overview & Architecture

The `MixerStrip` component places the mixer strip definitions in `skin/defs_mixer.json` (imported by `skin/TUI.json`) using the project's standard component family model:
- **One shared definition per architecture variant**:
  - `mixerStripSource`: Generator & Filter strips (SUB, A, B, C, NOISE, F1, F2).
  - `mixerStripBus`: Effect return buses (BUS 1, BUS 2).
  - `mixerStripMaster`: Main output console channel (MAIN / MASTER).
- **Independent handle remapping on instances**:
  Each channel strip instance references its shared variant definition and maps logical child handles (`Solo`, `Mute`, `Destination`, `Level`, `Peak`, `Hold`, `Pan`, `Send1`, `Send2`) to engine parameters and metering offsets.
- **Strict handle validation**:
  Attempting to instantiate a strip with missing required handles raises an explicit `ValueError`.
- **Usable touch targets**:
  All interactive touch targets are strictly $\ge 56\text{ px}$ in both dimensions (width $\ge 56\text{ px}$, height $\ge 56\text{ px}$).
- **Geometry compliance**:
  10 channel strips comfortably fit within the 1280 px canvas:
  $\text{left margin } 10\text{ px} + 9 \times 127\text{ px stride} + 115\text{ px width} = 1268\text{ px} \le 1280\text{ px}$.
- **Zero invented controls & zero Q-Link chips**:
  Follows hardware visual policy (0 numbered chips, no dedicated chip lanes, clean dark theme).

## Solo & Mute (S/M) Button Replacement

Following owner feedback, the header toggle switch is replaced with authentic mixer-style pushbuttons:
- **Solo (S) and Mute (M)**:
  - Width: 56 px each, Height: 56 px each.
  - Spacing: 3 px gap between S and M ($56 + 3 + 56 = 115\text{ px}$).
  - Strictly $\ge 56\text{ px}$ touch target in both dimensions.
- **Master Strip Adaptation**:
  - In audio consoles, the Master channel does not have a Solo function. To avoid a misleading dead button, the Master strip features a functional **MAIN FX Enable** button ($56 \times 56\text{ px}$) alongside the **Master Mute** button ($56 \times 56\text{ px}$).
- **Active States & Styling**:
  - **Solo (S)**: Subtle dark inactive state (`#151a2d`); vibrant amber active state (`#e5a93c`, outline `#ffcf60`) with crisp "S".
  - **Mute (M)**: Subtle dark inactive state (`#151a2d`); vibrant red active state (`#d31145`, outline `#ff4f4f`) with crisp "M".
  - **FX Enable**: Subtle dark inactive state; cyan/blue active state (`#4c8dff`, outline `#2ee6f0`) with crisp "FX".
- **Structural Touch Safety (No Overlapping Labels)**:
  - S, M, and FX letters are rendered directly on the button face images (`onImage` / `offImage`).
  - No sibling `Label` overlays the buttons, eliminating touch stealing and hidden caption bugs on hardware.

## Static Background & Real Fader Scale / Tick Marks

Understated blue/white SynthyBoy console styling places all static furniture onto the background image once:
- **Static Background Canvas**:
  - Tab background: solid page colour `ff090d19` (no bitmap).
  - Variant backgrounds: `mixer_strip_source_bg.png`, `mixer_strip_bus_bg.png`, `mixer_strip_master_bg.png` ($115 \times 606\text{ px}$).
  - Retains the required DRM plane sentinel at $(900, 18)$ with color `#141a35`.
- **Fader Scale Marks strictly follow `param_table.cpp`**:
  - **SOURCE generators (SUB, A, B, C, NOISE)**:
    - Linear percentage scale ($0\% \dots 100\%$).
    - Major ticks: $0, 25, 50, 75, 100\%$.
    - **Unity reference**: Highlighted $100\%$ tick (thicker line, brighter white `#ffffff`, bold label "100").
  - **FILTERS (F1, F2)**:
    - Linear dB scale ($-60\text{ dB} \dots +12\text{ dB}$, skew $1.0$).
    - Major ticks: $-60, -36, -24, -12, -6, 0, +6, +12\text{ dB}$.
    - **Unity reference**: Highlighted $0\text{ dB}$ tick at proportion $60/72 \approx 0.8333$ travel.
  - **BUS & MASTER (BUS 1, BUS 2, MAIN)**:
    - Tall 470 px fader with exponential skew ($2.5$) over $-60\text{ dB} \dots +6\text{ dB}$.
    - $\text{norm} = \left(\frac{\text{dB} + 60}{66}\right)^{2.5}$.
    - Major ticks: $-60\text{ (-inf)}, -48, -36, -24, -18, -12, -6, 0, +6\text{ dB}$.
    - **Unity reference**: Highlighted $0\text{ dB}$ tick at $\text{norm} = (60/66)^{2.5} \approx 0.7885$ (thicker tick, brighter `#ffffff`, bold label "0").

## Host-Only Normal Mixer & Overlay Gating

Following explicit owner directive, the normal Mixer tab is rendered **100% by the MPC native host JSON + static background image**:
- **Zero Overlay Graphics on Normal Mixer**:
  In `shell-mpc/overlay/mixer_ui.h`, when `nativeBase()` and `hostBackground()` are active, the overlay draws nothing on the strips. The normal Mixer tab overlay scene contains 0 draw commands, 0 vertices, and 0 overlay pixels.
- **Direct Touch Routing**:
  `covers(x, y)` returns `false` on the normal mixer tab, so 100% of touch and wheel events pass directly to host native widgets without overlay interception or touch stealing.

## Real Native Vertical Fader & Below-Track Value

Conforms to Auto MPC SDK Section 6.1 Slider (`sliderType: "TrackAndThumb"`, `direction: "Vertical"`):
- **Below-Track Value Readout**:
  The physical slider track groove is bounded to $168\text{ px}$ height on SOURCE strips and $384\text{ px}$ on BUS/MASTER strips. The dynamic value label is placed strictly **BENEATH** the slider track ($y: 172..196\text{ px}$ for source, $y: 388..412\text{ px}$ for bus/master). The fader thumb travels only within the track and **never clips or obscures the value readout**.
- Uses the fader cap asset `mixer_fader_cap.png` ($48 \times 26\text{ px}$), rendered by `fader.render_fader_cap` as the drawn `ui::Fader` thumb: the theme panel gradient, the keylineHi outlines and the centre index line, from the shared theme tokens.
- Background groove and scale markings remain static on the background; only the thumb moves with the parameter.

## Compact Native Dials (PAN, B1, B2)

Replaces prior horizontal sliders with authentic compact dials reusing the public factory APIs in `components.native.pan.pan` and `components.native.level.level`:
- **PAN**: Bipolar compact dial with center detent, using the approved `play_sub_pan_40.png` 40px filmstrip (127 frames).
- **B1 / B2 Sends**: Unipolar compact level dials ($0\dots 100\%$), using the approved `play_sub_level_40.png` 40px filmstrip (127 frames).
- **Usable Geometry in 115 px Width**:
  - Focus cell: $115 \times 56\text{ px}$ (touch target $\ge 56\text{ px}$).
  - Knob: $40 \times 40\text{ px}$ square at $(4, 8)$.
  - Caption label: at $(48, 6, 63, 18)$.
  - Value readout: at $(48, 26, 63, 22)$.
- Configured through supported rect APIs without modifying base dial implementations.

## Visual Previews (Offline Design Previews)

### Before / After Single Strip Comparison
Historical layout crop (exact geometry $x: 6..129, y: 176..734$) compared directly against the authentic new native `MixerStrip` render with identical logical state (SUB, Filters 1+2, 50.0% Level, 0.0% Pan, 0.0% Sends):

![MixerStrip Before/After Comparison](img/source_strip_before_after.png)

### Three Component Variants Side-by-Side
Side-by-side view of SOURCE, BUS, and MASTER showing all child controls without clipping:

![MixerStrip Variants Comparison](img/variants_comparison.png)

### Catalog Sample Card
Offline sample preview card for catalog:

![MixerStrip Catalog Card](img/catalog-mixer-strip.png)

## Component Variants Specification

| Variant | Target Strips | Included Child Controls | Level Fader Height | Omitted Controls | Remapped Logical Handles |
|---|---|---|---|---|---|
| **SOURCE** | SUB, OSC A, OSC B, OSC C, NOISE, F1, F2 | Strip name plate, S/M buttons, Destination selector, Level fader, Peak/Hold meter, Pan slider, Send 1 slider, Send 2 slider | 254 px | None | `Solo`, `Mute`, `Destination`, `Level`, `Peak`, `Hold`, `Pan`, `Send1`, `Send2` |
| **BUS** | BUS 1, BUS 2 | Strip name plate, S/M buttons, Destination selector, Level fader, Peak/Hold meter | 470 px | Pan, Send 1, Send 2 | `Solo`, `Mute`, `Destination`, `Level`, `Peak`, `Hold` |
| **MASTER** | MAIN / MASTER | Strip name plate, FX Enable button, Mute button, Level fader, Peak/Hold meter | 470 px | Destination, Pan, Send 1, Send 2 | `FxEnable`, `Mute`, `Level`, `Peak`, `Hold` |

### Gating Semantics & Decoupled Muting
- **SOURCE strips**: Power (`source.on`) gates routing destinations, pan, and sends (`power_gate`).
- **Mute (`M`)**: Decoupled from fader editing and meter readout. Level faders and live meters remain fully interactive and monitored even when a channel is muted.
- **BUS & MASTER strips**: Dry audio routing, level control, and metering are NOT gated by FX bypass. Level fader and meter remain fully active. Master Mute zeroes master output gain without mutating `main.fx_bypass` or parameters.

## Routing-Aware Solo & Mute Engine Implementation

The engine defines dedicated non-mutating atomic solo and mute bitmasks (`mixerSoloMask_`, `mixerMuteMask_`) in `OwnEngine`, dispatched via `IInstanceView` and `native_ui_params.h` (`kMixerControl`, handles `0..19` for 10 Solo and 10 Mute channels):
1. **Zero Parameter Corruption**: Muting or soloing operates entirely on runtime DSP bitmasks. Saved patches, presets, `source.on`, `filter.on`, levels, and `fx_bypass` parameters are never mutated.
2. **Routing Graph Awareness**:
   - **Oscillator feeds active Filters/Buses**: Soloed oscillators feed downstream filters and buses without silencing upstream components.
   - **Filter Solo Preserves Feeders**: When Filter 1 or Filter 2 is soloed, oscillators feeding that filter remain active internally into the filter stage, but their direct-to-master or cross-filter paths are silenced.
   - **Bus Solo Preserves Feeders**: Soloed buses preserve feeder audio while non-soloed paths are attenuated.
   - **Multi-Path Non-Solo Suppression**: Feeder contributions directed to un-soloed buses/destinations are silenced while the solo path is preserved.
   - **Disabled Source Gating (No False Solo)**: Disabled sources (`source.on == 0`) are excluded from `effectiveSolo` calculation, preventing false solo states from silencing active audio.
   - **Master Mute**: Silences final audio rendering output directly in real-time DSP without allocating memory, acquiring locks, or mutating `main.fx_bypass`.

## Fader Scales & Tick Calibration

- **SOURCE generators (SUB, A, B, C, NOISE)**:
  - Linear percentage scale ($0\% \dots 100\%$, 100% unity highlight).
- **FILTERS (F1, F2)**:
  - Linear dB scale ($-60\text{ dB} \dots +12\text{ dB}$, skew 1.0, 0 dB unity highlight).
  - Explicit "-60" bottom label (no "-inf" since minimum gain is finite -60 dB).
- **BUS & MASTER (BUS 1, BUS 2, MAIN)**:
  - Tall 470 px fader with exponential skew ($2.5$) over $-60\text{ dB} \dots +6\text{ dB}$ (0 dB unity highlight).
  - Bottom labels thinned to `(-60, -30, -18, -6, 0, 6)` with `-48` as unlabelled minor tick mark, eliminating text collision.

## Layout

- **Channel Strips**: Start cleanly at top (`topY = 10`, `height = 606`: the strips take the whole tab; the Mixer is one screen, the peek action row that took the bottom 56 px is gone).
- **Shared Tokens**: All geometry reads strictly from `components/sizes.json` (`mixer_strip.*`). S/M button colors read from named tokens in `components/colors.json` (`dial.value`, `accent.cyan`, `focus.field`, `accent.red`, `button.surface`). Zero hardcoded hex colors or fallback styles.

## Host-Only Normal Mixer & Overlay Gating

- **Normal Mixer Screen**: Completely host-only when `nativeBase()` is active (`useNativeCells()` in `shell-mpc/overlay/mixer_ui.h`). Emits 0 ops, 0 vertices, and empty scene. `covers(x, y)` returns `false` so the host receives 100% of input events.
- **Destination List**: Opening a strip's destination field triggers `ListOpen` (`native_choice_list.h`) and displays the shared plugin modal `nativeListModal` (`Parameter 3903`; [AGENTS.md](../../AGENTS.md#lists-owner-2026-11-28)) above the Mixer tab.

## Verification & Test Results

All targeted tests and offline previews pass cleanly:
- `synthyboy_test_own_engine_fixes`: 17 test cases, 828 checks, 0 failed (0.18s) - verifies `own_mixer_solo_routing_and_suppression`, `own_mixer_solo_filter_preserves_feeders`, `own_mixer_mute_silences_without_mutating_params`, `own_mixer_master_mute_independent_of_fx_bypass`, and `own_mixer_disabled_source_no_false_solo`.
- `mixer_native_ui_host`: All assertions passed - verifies native base handoff, opaque fallback, 0-op overlay scene, and destination popups.
- `python3 scripts/tests/test_ui_screen_mixer.py`: 6 tests, OK (5.2s).
- `python3 scripts/tests/test_native_component_isolation.py mixer_strip`: 2 tests, OK (0.18s).
- Offline design preview generation: Regenerated `catalog-mixer-strip.png`, `source_strip_before_after.png`, `variants_comparison.png`, `overlay_bg_mixer.png`, and `mixer-preview.png`.

## Style Tokens

All dimensions, rects, font sizes, and colors are defined in `components/sizes.json` (strictly prefixed with `mixer_strip.*`) and `components/colors.json`, read via `components/style.py`. No numbers or font sizes are hardcoded.

## How to Re-generate Previews

```sh
python3 scripts/render_native_mixer_strip_preview.py
python3 scripts/render_mixer_preview.py
```
Pre-commit catalog check uses `components/native/mixer_strip/catalog.json` (`approved: false` pending owner visual review).
