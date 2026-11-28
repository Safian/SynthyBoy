# UI Map: Mixer Page
Refreshed 2026-11-26: the Mixer is one screen of ten strips; the routing bar with its five PEEK buttons and the docked Filter and FX editors are gone (the Filter and the FX editors are tabs of their own).

## Purpose

The Mixer page is the full-screen MPC overlay view for the "Mixer" tab (view kind `mixer`, registered as page element `MixerUi::kElementPage = 5` in `shell-mpc/overlay/mixer_ui.h:36`). It presents a complete 10-channel mixing console spanning the entire tab canvas:
- 10 channel strips side by side: Sub oscillator (`SUB`), Oscillator A (`A`), Oscillator B (`B`), Oscillator C (`C`), Noise generator (`NOISE`), Filter 1 (`F1`), Filter 2 (`F2`), Effect Bus 1 (`BUS1`), Effect Bus 2 (`BUS2`), and Master output (`MASTER`).
- No routing bar and no tab bar: the ten strips take the whole tab. The destination of a strip is its dropdown; a tap on a strip selects it and makes the Q group of its level fader the active one.
- Channel controls include power or rack bypass, destination, level and a peak/hold meter. Sources and filters also have bipolar PAN and sends to BUS 1 and BUS 2.
- With the factory host background active, the MPC owns all 50 strip controls and the 10 meters. The overlay draws the Q-Link chips and the destination popup list. It also supplies the complete Mixer when the native background is unavailable. [Mixer skin](../../scripts/mpc-skin-extras/84_mixer.py), [Mixer runtime](../../shell-mpc/overlay/mixer_ui.h)
- Turning a source or filter off leaves its power switch available and displays its other controls in grey. Those fields and the corresponding LEVEL Q-Link slot stop accepting edits until the strip is switched back on. On BUS 1, BUS 2, and MASTER, the switch bypasses the FX rack and the dry level, metering, and available routing stay active.

## Layout

The page occupies the full MPC overlay tab region of 1280 x 628 pixels, starting below the MPC host header at vertical coordinate y = 110 (`shell-mpc/overlay/overlay_layout.json:639-648`, `shell-mpc/overlay/mixer_ui.h:40,72`):
- Canvas bounds: width 1280 px, height 628 px (placed at x = 0, y = 110, w = 1280, h = 628).
- Channel Strips (`MixerStrip strips[10]`, `shell-mpc/ui/mixer_screen.h:258-268`):
  - Placed from y = 10 to 12 px above the tab bottom (topY = 120, height = 606 px; the skin tokens `mixer_strip.start_y` and `mixer_strip.height`).
  - 10 strips with gap = 4 px, each strip width = 123 px (`(1268 - 9 * 4) / 10 = 123`).
  - Vertical section dividing keylines (1.5 px wide, `shell-mpc/ui/mixer_screen.h:271-283`) are drawn after strip 4 (`NOISE`), strip 6 (`F2`), and strip 8 (`BUS2`), grouping the console into Generator sources (Sub, A, B, C, Noise), Filter sources (F1, F2), FX Buses (BUS1, BUS2), and Final output (MASTER).
- Strip Internal Structure (`MixerStrip::layout()`, `shell-mpc/ui/mixer_strip.h:470-500`):
  - Strip Header:
    - Source power and rack FX bypass buttons are 56 px high. The rack switches read BUS 1 FX, BUS 2 FX, and MAIN FX.
    - Destination dropdown: 56 px high. Displays the current route.
  - Q-Link Area:
    - Starts below the destination button with a lane reserved for Q-Link index chips (`kQLane`, `shell-mpc/ui/mixer_strip.h:479`). Only mapped LEVEL faders can carry chips; the lanes over PAN, B1 and B2 stay as spacing (the chips are not drawn there, see Q-Link groups).
    - Strips 0 to 6 (`SUB`, `A`, `B`, `C`, `NOISE`, `F1`, `F2`):
      - Vertical LEVEL control: 254 px high, with its value below and a peak/hold meter alongside.
      - PAN slider: 56 px high, with a bipolar track and the current pan value.
      - Bus 1 Send slider (`SliderBar send1`, `shell-mpc/ui/mixer_strip.h:417,489`): height 56 px, unipolar track, label `B1` and percentage readout.
      - Bus 2 Send slider (`SliderBar send2`, `shell-mpc/ui/mixer_strip.h:418,491`): height 56 px, unipolar track, label `B2` and percentage readout.
    - Strips 7 to 9 (`BUS1`, `BUS2`, `MASTER`):
      - Have no Pan, Send 1, or Send 2 controls (`shell-mpc/overlay/mixer_ui.h:63-67`, `shell-mpc/ui/mixer_strip.h:437-443`).
      - Vertical LEVEL control: 470 px high, with a dB readout and a peak/hold meter alongside.

## Controls

| Label on screen | Parameter key | Gesture | Effect | Source |
| --- | --- | --- | --- | --- |
| Strip focus | - | Tap | Selects the strip (the routing line follows it) and makes the Q group of its LEVEL fader the active one (published at the touch, `shell-mpc/overlay/mixer_ui.h:226`) | `shell-mpc/ui/mixer_screen.h:201-214` |
| SUB | `sub.on` | Tap | Toggles Sub oscillator on / off | `shell-mpc/overlay/mixer_ui.h:321` |
| Route | `sub.destination` | Tap | Opens destination popup list (Filter 1, Filter 2, Filters 1+2, Effects only, Direct out, Filter blend, None) | `shell-mpc/overlay/mixer_ui.h:321` |
| LEVEL | `sub.level` | Drag vertical / Q-Link (levels group) | Adjusts Sub oscillator output level (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:321` |
| PAN | `sub.pan` | Drag horizontal | Adjusts Sub stereo panning (L 100 % to R 100 %) | `shell-mpc/overlay/mixer_ui.h:321` |
| B1 | `sub.bus1_send` | Drag horizontal | Adjusts Sub send level to FX Bus 1 (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:321` |
| B2 | `sub.bus2_send` | Drag horizontal | Adjusts Sub send level to FX Bus 2 (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:321` |
| A | `osc1.on` | Tap | Toggles Oscillator A on / off | `shell-mpc/overlay/mixer_ui.h:322` |
| Route | `osc1.destination` | Tap | Opens destination popup list (Filter 1, Filter 2, Filters 1+2, Effects only, Direct out, Filter blend, None) | `shell-mpc/overlay/mixer_ui.h:322` |
| LEVEL | `osc1.level` | Drag vertical / Q-Link (levels group) | Adjusts Oscillator A output level (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:322` |
| PAN | `osc1.pan` | Drag horizontal | Adjusts Oscillator A stereo panning (L 100 % to R 100 %) | `shell-mpc/overlay/mixer_ui.h:322` |
| B1 | `osc1.bus1_send` | Drag horizontal | Adjusts Oscillator A send level to FX Bus 1 (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:322` |
| B2 | `osc1.bus2_send` | Drag horizontal | Adjusts Oscillator A send level to FX Bus 2 (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:322` |
| B | `osc2.on` | Tap | Toggles Oscillator B on / off | `shell-mpc/overlay/mixer_ui.h:323` |
| Route | `osc2.destination` | Tap | Opens destination popup list (Filter 1, Filter 2, Filters 1+2, Effects only, Direct out, Filter blend, None) | `shell-mpc/overlay/mixer_ui.h:323` |
| LEVEL | `osc2.level` | Drag vertical / Q-Link (levels group) | Adjusts Oscillator B output level (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:323` |
| PAN | `osc2.pan` | Drag horizontal | Adjusts Oscillator B stereo panning (L 100 % to R 100 %) | `shell-mpc/overlay/mixer_ui.h:323` |
| B1 | `osc2.bus1_send` | Drag horizontal | Adjusts Oscillator B send level to FX Bus 1 (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:323` |
| B2 | `osc2.bus2_send` | Drag horizontal | Adjusts Oscillator B send level to FX Bus 2 (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:323` |
| C | `osc3.on` | Tap | Toggles Oscillator C on / off | `shell-mpc/overlay/mixer_ui.h:324` |
| Route | `osc3.destination` | Tap | Opens destination popup list (Filter 1, Filter 2, Filters 1+2, Effects only, Direct out, Filter blend, None) | `shell-mpc/overlay/mixer_ui.h:324` |
| LEVEL | `osc3.level` | Drag vertical / Q-Link (levels group) | Adjusts Oscillator C output level (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:324` |
| PAN | `osc3.pan` | Drag horizontal | Adjusts Oscillator C stereo panning (L 100 % to R 100 %) | `shell-mpc/overlay/mixer_ui.h:324` |
| B1 | `osc3.bus1_send` | Drag horizontal | Adjusts Oscillator C send level to FX Bus 1 (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:324` |
| B2 | `osc3.bus2_send` | Drag horizontal | Adjusts Oscillator C send level to FX Bus 2 (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:324` |
| NOISE | `noise.on` | Tap | Toggles Noise generator on / off | `shell-mpc/overlay/mixer_ui.h:325` |
| Route | `noise.destination` | Tap | Opens destination popup list (Filter 1, Filter 2, Filters 1+2, Effects only, Direct out, Filter blend, None) | `shell-mpc/overlay/mixer_ui.h:325` |
| LEVEL | `noise.level` | Drag vertical / Q-Link (levels group) | Adjusts Noise generator output level (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:325` |
| PAN | `noise.pan` | Drag horizontal | Adjusts Noise generator stereo panning (L 100 % to R 100 %) | `shell-mpc/overlay/mixer_ui.h:325` |
| B1 | `noise.bus1_send` | Drag horizontal | Adjusts Noise send level to FX Bus 1 (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:325` |
| B2 | `noise.bus2_send` | Drag horizontal | Adjusts Noise send level to FX Bus 2 (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:325` |
| F1 | `filter1.on` | Tap | Toggles Filter 1 on / off | `shell-mpc/overlay/mixer_ui.h:326` |
| Route | `filter1.destination` | Tap | Opens destination popup list (Effects only, Direct out, None) | `shell-mpc/overlay/mixer_ui.h:326` |
| LEVEL | `filter1.level` | Drag vertical / Q-Link (levels group) | Adjusts Filter 1 output level (-60 dB to +12 dB) | `shell-mpc/overlay/mixer_ui.h:326` |
| PAN | `filter1.pan` | Drag horizontal | Adjusts Filter 1 stereo panning (L 100 % to R 100 %) | `shell-mpc/overlay/mixer_ui.h:326` |
| B1 | `filter1.bus1_send` | Drag horizontal | Adjusts Filter 1 send level to FX Bus 1 (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:326` |
| B2 | `filter1.bus2_send` | Drag horizontal | Adjusts Filter 1 send level to FX Bus 2 (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:326` |
| F2 | `filter2.on` | Tap | Toggles Filter 2 on / off | `shell-mpc/overlay/mixer_ui.h:327` |
| Route | `filter2.destination` | Tap | Opens destination popup list (Effects only, Direct out, None) | `shell-mpc/overlay/mixer_ui.h:327` |
| LEVEL | `filter2.level` | Drag vertical / Q-Link (levels group) | Adjusts Filter 2 output level (-60 dB to +12 dB) | `shell-mpc/overlay/mixer_ui.h:327` |
| PAN | `filter2.pan` | Drag horizontal | Adjusts Filter 2 stereo panning (L 100 % to R 100 %) | `shell-mpc/overlay/mixer_ui.h:327` |
| B1 | `filter2.bus1_send` | Drag horizontal | Adjusts Filter 2 send level to FX Bus 1 (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:327` |
| B2 | `filter2.bus2_send` | Drag horizontal | Adjusts Filter 2 send level to FX Bus 2 (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:327` |
| BUS1 | `bus1.fx_bypass` | Tap | Toggles Bus 1 FX bypass (on = active FX, off = bypassed) | `shell-mpc/overlay/mixer_ui.h:328` |
| Route | `bus1.destination` | Tap | Opens destination popup list (Main, Direct, Other bus) | `shell-mpc/overlay/mixer_ui.h:328` |
| LEVEL | `bus1.level` | Drag vertical / Q-Link (levels group) | Adjusts FX Bus 1 return level (-60 dB to +6 dB, fine scale) | `shell-mpc/overlay/mixer_ui.h:328` |
| BUS2 | `bus2.fx_bypass` | Tap | Toggles Bus 2 FX bypass (on = active FX, off = bypassed) | `shell-mpc/overlay/mixer_ui.h:329` |
| Route | `bus2.destination` | Tap | Opens destination popup list (Main, Direct, Other bus) | `shell-mpc/overlay/mixer_ui.h:329` |
| LEVEL | `bus2.level` | Drag vertical / Q-Link (levels group) | Adjusts FX Bus 2 return level (-60 dB to +6 dB, fine scale) | `shell-mpc/overlay/mixer_ui.h:329` |
| MASTER | `main.fx_bypass` | Tap | Toggles Main FX rack bypass (on = active FX, off = bypassed) | `shell-mpc/overlay/mixer_ui.h:330` |
| LEVEL | `master.volume` | Drag vertical / Q-Link (levels group) | Adjusts Master output volume (-60 dB to +6 dB, fine scale with peak hold) | `shell-mpc/overlay/mixer_ui.h:330` |

## Opens

The destination button opens the MPC host menu on the native Mixer. The overlay fallback opens a modal list with the same engine choices. A selection writes the destination parameter of that strip. [Mixer skin](../../scripts/mpc-skin-extras/84_mixer.py), [Mixer runtime](../../shell-mpc/overlay/mixer_ui.h)

The native strip controls stay active if the background sentinel is unavailable. The foreground fills the background around their transparent cells. The standalone overlay fallback supplies the controls when running without the native host widgets.

- SUB, A, B, C and NOISE offer Filter 1, Filter 2, Filters 1+2, Effects only, Direct out, Filter blend, and None.
- F1 and F2 offer Effects only, Direct out, and None.
- BUS 1 and BUS 2 offer Main, Direct, and Other bus.

Note (bold cut, 2026-11-21): the MASTER strip (strip 9) has NO destination button: `bindings_[9].destParam = -1` (`shell-mpc/overlay/mixer_ui.h:330`), so `MixerStrip::setHasDestination (false)` is called in the `MixerUi` constructor (`shell-mpc/overlay/mixer_ui.h:66`), which calls `show (destButton, false)` (`shell-mpc/ui/mixer_strip.h:446`) and lays the button out as an empty rectangle (`shell-mpc/ui/mixer_strip.h:476`) — the button is completely absent (not drawn at all), no rectangle, no touch; the cell stays empty so the faders of BUS1, BUS2 and MASTER keep one top line. The page has 50 controls (51 before). Details and pictures: `docs/ui-map/mixer-master-bold-cut/README.md`.

## Q-Link groups

Levels-only layout (Q-Link mapping extension, merged 2026-11-21; `docs/ui-map/q-link-audit.md` status row "Mixer levels-only layout", pictures `docs/screens/mixer.jpg`). The page has 3 groups, not one per strip (`MixerFullScreen::kMaxGroups = 3`, `kStripOfSlot`, `shell-mpc/ui/mixer_screen.h:130-134`; names `LEVELS SUB A B C`, `LEVELS NOISE F1 F2 MAIN`, `LEVELS BUS 1 2`, `shell-mpc/ui/mixer_screen.h:179-183`). The table below describes the four-knob banks. Every member is the LEVEL fader of a strip; PAN, B1, B2, the destinations and the power switches are touch only (no chip, no Q slot):

| Group | Slot 1 | Slot 2 | Slot 3 | Slot 4 | Frame |
| --- | --- | --- | --- | --- | --- |
| G1 `LEVELS SUB A B C` | `sub.level` (SUB) | `osc1.level` (A) | `osc2.level` (B) | `osc3.level` (C) | framed |
| G2 `LEVELS NOISE F1 F2 MAIN` | `noise.level` (NOISE) | `filter1.level` (F1) | `filter2.level` (F2) | `master.volume` (MAIN) | frameless: BUS 1 and BUS 2 lie between F2 and MAIN (`shell-mpc/ui/mixer_screen.h:293`) |
| G3 `LEVELS BUS 1 2` | `bus1.level` | `bus2.level` | - | - | framed, two slots empty |

- The LEVEL targets come from `MixerUi::publishQGroup`. Each slot holds the LEVEL parameter of its active strip or is empty if the strip is off. Both `publishQGroup` and `setupGroups` (`shell-mpc/ui/mixer_screen.h:288-294`) use the same table. Group of a strip: `groupOfStrip` / `slotOfStrip` (`shell-mpc/ui/mixer_screen.h:133-134`).
- Overlay-handled touches on a strip select it and activate its level bank. `MixerUi::touch` publishes the selection immediately. Native widget gestures reach the host through the aperture. The published XL slots stay fixed. `setActiveGroup` keeps the selected strip when it belongs to the group and otherwise selects the first strip. Sources: `shell-mpc/overlay/mixer_ui.h` and `shell-mpc/ui/mixer_screen.h`.
- All ten strips have LEVEL parameters. The slots of inactive source and filter strips stay empty in place. The bus and master slots stay active during FX rack bypass. [Mixer runtime](../../shell-mpc/overlay/mixer_ui.h)

**MPC XL (16 knobs).** No active group: with at most four groups, group g sits on Q 4g+1 .. 4g+4 (`QRouter::blockOfGroup`, `shell-mpc/q_router.h:71-79`: `wide () && nGroups <= kGroups` forces the active group to 0). So Q1 SUB, Q2 A, Q3 B, Q4 C; Q5 NOISE, Q6 F1, Q7 F2, Q8 BUS 1, Q9 BUS 2, Q10 MAIN (`master.volume`); Q11 .. Q16 have no target. The native draw path can draw mapped LEVEL chips at their absolute slots. `drawStripMapped` draws the later blocks, while the active group draws through `QGroup` (`shell-mpc/ui/mixer_screen.h`, `shell-mpc/overlay/mixer_ui.h`).

**Key 61 (four knobs, banks).** Bank n = Q 4n-3 .. 4n of the same 16-slot list (`docs/MPC.md` "Q groups"). The ACTIVE group (the one of the last touched strip) is bank 1; the other groups follow in order and wrap (`QRouter::mapGroups`, `shell-mpc/overlay/q_publish.h:57-65`). Example, G1 active: bank 1 = SUB A B C, bank 2 = NOISE F1 F2 MAIN, bank 3 = BUS 1, BUS 2, bank 4 empty; after a touch on F1: bank 1 = NOISE F1 F2 MAIN, bank 2 = BUS 1, BUS 2, bank 3 = SUB A B C. The chips of the non-active groups show slot numbers 1 .. 4 (`QGroup::wideDevice` false, `shell-mpc/ui/mixer_screen.h:237,246`).

Host coverage for both layouts is in `shell-mpc/overlay/tests/qslots_host.cpp` (`testMixer`) and `shell-mpc/overlay/tests/mixfx_ui_host.cpp` (`testMixerQPerStrip`). Native apertures and modal ownership are covered by `shell-mpc/overlay/tests/mixer_native_ui_host.cpp`.

## Live parts

- All ten meters read `stripPeak` from the engine. Each has a peak bar and a 2 px hold marker. The hold falls at 0.5 of the display range per second while a peak stays below it. The shell reports changed peak and hold values to the host. [Mixer runtime](../../shell-mpc/overlay/mixer_ui.h), [MPC shell](../../shell-mpc/synthyboy_mpc.cpp)
- There is no peek: the Mixer publishes only its own level groups, and the Filter and FX editors are reached by their own tabs. A host drawer needs no panel handling. [Mixer screen](../../shell-mpc/ui/mixer_screen.h), [Mixer runtime](../../shell-mpc/overlay/mixer_ui.h)

## Duplicates

43 of the 50 parameter keys controlled on the Mixer page are direct duplicates of controls on other overlay pages or tabs:

1. Oscillator & Noise Channels (Strips 0 to 4):
   - `sub.on`, `sub.level`, `sub.pan`, `sub.destination`, `sub.bus1_send`, `sub.bus2_send`:
     - Also on Osc page (`shell-mpc/overlay/osc_ui.h:492-495,518-519,815-818`): power toggle, pan knob, level knob on compact and full views; destination route button; destination, bus1_send, bus2_send in Osc routing detail popup.
     - Also on Play page (`shell-mpc/play_params.h:55`): `sub.pan` and `sub.level` proxy knobs when Play Osc selector is set to Sub.
   - `osc1.on`, `osc1.level`, `osc1.pan`, `osc1.destination`, `osc1.bus1_send`, `osc1.bus2_send`:
     - Also on Osc page (`shell-mpc/overlay/osc_ui.h:492-495,518-519,815-818`): Osc A power toggle, pan knob, level knob on compact and full views; destination route button; destination, bus1_send, bus2_send in Osc routing detail popup.
     - Also on Play page (`shell-mpc/play_params.h:40`): `osc1.pan` and `osc1.level` proxy knobs when Play Osc selector is set to Osc A.
   - `osc2.on`, `osc2.level`, `osc2.pan`, `osc2.destination`, `osc2.bus1_send`, `osc2.bus2_send`:
     - Also on Osc page (`shell-mpc/overlay/osc_ui.h:492-495,518-519,815-818`): Osc B power toggle, pan knob, level knob on compact and full views; destination route button; destination, bus1_send, bus2_send in Osc routing detail popup.
     - Also on Play page (`shell-mpc/play_params.h:40`): `osc2.pan` and `osc2.level` proxy knobs when Play Osc selector is set to Osc B.
   - `osc3.on`, `osc3.level`, `osc3.pan`, `osc3.destination`, `osc3.bus1_send`, `osc3.bus2_send`:
     - Also on Osc page (`shell-mpc/overlay/osc_ui.h:492-495,518-519,815-818`): Osc C power toggle, pan knob, level knob on compact and full views; destination route button; destination, bus1_send, bus2_send in Osc routing detail popup.
     - Also on Play page (`shell-mpc/play_params.h:40`): `osc3.pan` and `osc3.level` proxy knobs when Play Osc selector is set to Osc C.
   - `noise.on`, `noise.level`, `noise.pan`, `noise.destination`, `noise.bus1_send`, `noise.bus2_send`:
     - Also on Osc page (`shell-mpc/overlay/osc_ui.h:492-495,518-519,815-818`): Noise power toggle, pan knob, level knob on compact and full views; destination route button; destination, bus1_send, bus2_send in Osc routing detail popup.
     - Also on Play page (`shell-mpc/play_params.h:56`): `noise.pan` and `noise.level` proxy knobs when Play Osc selector is set to Noise.

2. Filter Channels (Strips 5 and 6):
   - `filter1.on`, `filter1.level`, `filter1.pan`, `filter1.destination`, `filter1.bus1_send`, `filter1.bus2_send`:
     - Also on Filter page (`shell-mpc/overlay/filter_ui.h:302-327`): Filter 1 power toggle (full screen power), output level (full screen level in OUT group and route popup level), stereo pan (full screen pan and route popup pan), destination (route popup destination), and bus sends (route popup bus1 and bus2).
   - `filter2.on`, `filter2.level`, `filter2.pan`, `filter2.destination`, `filter2.bus1_send`, `filter2.bus2_send`:
     - Also on Filter page (`shell-mpc/overlay/filter_ui.h:302-327`): Filter 2 power toggle, output level, stereo pan, destination, and bus sends.

3. Master Channel (Strip 9):
   - `master.volume`:
     - Also on Master page (`shell-mpc/overlay/master_ui.h:243`): Volume knob in the OUTPUT group.

4. Parameters Unique to the Mixer Page (No duplicates):
   - `bus1.fx_bypass`, `bus1.level`, `bus1.destination` (Strip 7 `BUS1`): only accessible on the Mixer page.
   - `bus2.fx_bypass`, `bus2.level`, `bus2.destination` (Strip 8 `BUS2`): only accessible on the Mixer page.
   - `main.fx_bypass` (Strip 9 `MASTER`): only accessible on the Mixer page.

## Questionable

Judgements based on code inspection:

1. FIXED (bold cut): non-functional, disabled "OUT" destination button on Master strip (was in `shell-mpc/overlay/mixer_ui.h`; the button is now hidden, `shell-mpc/overlay/mixer_ui.h:157-158`). Old finding:
   - `bindings_[9].destParam` is -1, so `strip.destButton.setLabel("OUT")` and `strip.destButton.setEnabled(false)` were executed. The button occupied a full 56 px tall touch cell in the MASTER strip layout (`shell-mpc/ui/mixer_strip.h:476`), styled identically to the interactive destination dropdowns of other strips. Tapping it does nothing. This is dead visual clutter that misleads the user into expecting an interactive routing choice.
2. Inverted FX bypass disguised as channel power switches (`shell-mpc/overlay/mixer_ui.h:57,104-108,328-330,379-383`):
   - The power toggle button on strips 0 to 6 controls actual channel power (`.on` parameters: 1 = active, 0 = off).
   - However, on strips 7 (`BUS1`), 8 (`BUS2`), and 9 (`MASTER`), the power toggle is bound to `bus1.fx_bypass`, `bus2.fx_bypass`, and `main.fx_bypass` with `powerInverted = true` (`shell-mpc/overlay/mixer_ui.h:328-330`). When the power icon is lit ("on"), bypass is 0 (effects enabled); when the power icon is dim ("off"), bypass is 1 (effects bypassed).
   - Tapping the "power" button of the MASTER channel does not mute or disable the synthesizer's master output—it merely bypasses the Main FX rack. This violates user expectation of a mixer master strip power/mute switch.
3. Severe layout asymmetry between source strips and bus/master strips (`shell-mpc/ui/mixer_strip.h:477-500`):
   - Strips 0 to 6 cram a fader (height ~190 px) and three 56 px slider cells (`pan`, `send1`, `send2`) into the strip.
   - Strips 7 to 9 completely omit pan and sends, so their faders stretch to the entire height of the strip (~390 px, `shell-mpc/ui/mixer_strip.h:497`).
   - This gives secondary bus returns and master volume double the physical fader travel and precision of the 7 primary sound sources (oscillators and filters), where fine level balancing is most critical.
4. To change the signal path, use the destination dropdown on the channel strip.
5. "None" destination choice creates a silent dead end (`docs/params.json`, `shell-mpc/ui/mixer_screen.h:116-117`):
   - All oscillator and filter destination choice lists include `None` (`docs/params.json`). When selected, audio is simply dropped. Since every strip already has a dedicated power toggle to silence it, having `None` as a routing target is confusing and redundant.
6. Extreme parameter redundancy across tabs (`shell-mpc/overlay/mixer_ui.h:321-330` vs `shell-mpc/overlay/osc_ui.h` and `shell-mpc/overlay/filter_ui.h`):
   - 86% of all controls on the Mixer page (43 out of 50 parameters; the old text said 42 and 84%: 30 source + 12 filter + `master.volume`) are identical duplicates of controls on the dedicated Osc, Filter, and Master tabs. Changing a send or destination can be done either in a channel strip or deep inside a popup modal on the source tab, causing confusion about which screen is the primary mixing location.
