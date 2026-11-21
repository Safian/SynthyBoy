# UI Map: Mixer Page

## Purpose

The Mixer page is the full-screen MPC overlay view for the "Mixer" tab (view kind `mixer`, registered as page element `MixerUi::kElementPage = 5` in `shell-mpc/overlay/mixer_ui.h:36`). It presents a complete 10-channel mixing console spanning the entire tab canvas:
- 10 channel strips side by side: Sub oscillator (`SUB`), Oscillator A (`A`), Oscillator B (`B`), Oscillator C (`C`), Noise generator (`NOISE`), Filter 1 (`F1`), Filter 2 (`F2`), Effect Bus 1 (`BUS1`), Effect Bus 2 (`BUS2`), and Master output (`MASTER`).
- A top routing overview bar (`MixerRoutingBar`, `shell-mpc/ui/mixer_screen.h:18`) displaying destination nodes and drawing animated signal flow lines from the currently selected channel strip.
- Comprehensive channel controls: channel power/bypass toggle, routing destination selector dropdown, vertical level fader with embedded peak level meter, bipolar stereo pan slider, and send level sliders to FX Bus 1 and Bus 2.

## Layout

The page occupies the full MPC overlay tab region of 1280 x 628 pixels, starting below the MPC host header at vertical coordinate y = 110 (`shell-mpc/overlay/overlay_layout.json:491-498`, `shell-mpc/overlay/mixer_ui.h:40,72`):
- Canvas bounds: width 1280 px, height 628 px (placed at x = 0, y = 110, w = 1280, h = 628).
- Signal Routing Bar (`MixerRoutingBar`, `shell-mpc/ui/mixer_screen.h:245-247`):
  - Placed at x = 6, y = 114, w = 1268, h = 56.
  - Displays 5 destination target chips along the top: `FLT 1` (x ~ 16%), `FLT 2` (x ~ 32%), `MAIN FX` (x ~ 52%), `DIRECT` (x ~ 72%), `MASTER` (x ~ 90%) (`shell-mpc/ui/mixer_screen.h:53-57`).
  - Renders a routing line from the bottom midpoint of the selected channel strip up to its target node or neighbouring bus strip.
- Channel Strips (`MixerStrip strips[10]`, `shell-mpc/ui/mixer_screen.h:249-260`):
  - Placed below the routing bar at topY = 176, height = 558 px.
  - 10 strips with gap = 4 px, each strip width = 123 px (`(1268 - 9 * 4) / 10 = 123`).
  - Vertical section dividing keylines (1.5 px wide, `shell-mpc/ui/mixer_screen.h:265-273`) are drawn after strip 4 (`NOISE`), strip 6 (`F2`), and strip 8 (`BUS2`), grouping the console into Generator sources (Sub, A, B, C, Noise), Filter sources (F1, F2), FX Buses (BUS1, BUS2), and Final output (MASTER).
- Strip Internal Structure (`MixerStrip::layout()`, `shell-mpc/ui/mixer_strip.h:466-498`):
  - Strip Header:
    - Power toggle button (`StripPower power`, `shell-mpc/ui/mixer_strip.h:333,470`): height 56 px. Contains power icon above and channel name label below.
    - Destination route dropdown button (`RouteDropdown destButton`, `shell-mpc/ui/mixer_strip.h:372,472`): height 56 px. Displays current destination label with a downward chevron.
  - Q-Link Area:
    - Starts below the destination button with a 19 px lane reserved for Q-Link index chips (`kQLane = 19`).
    - Strips 0 to 6 (`SUB`, `A`, `B`, `C`, `NOISE`, `F1`, `F2`):
      - Vertical Level Fader (`Fader fader`, `shell-mpc/ui/mixer_strip.h:16,481`): occupies remaining height above sliders (min 96 px, typically ~190 px). Contains dB or % graduation scale, draggable thumb cap, center index line, and integrated peak meter.
      - Pan slider (`SliderBar pan`, `shell-mpc/ui/mixer_strip.h:242,483`): height 56 px, bipolar track with center mark, label `PAN` and formatted text (`C`, `L xx`, `R xx`).
      - Bus 1 Send slider (`SliderBar send1`, `shell-mpc/ui/mixer_strip.h:485`): height 56 px, unipolar track, label `B1` and percentage readout.
      - Bus 2 Send slider (`SliderBar send2`, `shell-mpc/ui/mixer_strip.h:487`): height 56 px, unipolar track, label `B2` and percentage readout.
    - Strips 7 to 9 (`BUS1`, `BUS2`, `MASTER`):
      - Have no Pan, Send 1, or Send 2 controls (`shell-mpc/overlay/mixer_ui.h:63-64`).
      - Vertical Level Fader (`Fader fader`, `shell-mpc/ui/mixer_strip.h:493`): runs the full height of the strip from below the destination button down to the bottom margin (~390 px), using a fine dB scale.

## Controls

| Label on screen | Parameter key | Gesture | Effect | Source |
| --- | --- | --- | --- | --- |
| Strip focus | - | Tap | Selects active strip, focuses its 4 Q-Link slots, and highlights its routing line | `shell-mpc/ui/mixer_screen.h:192` |
| SUB | `sub.on` | Tap | Toggles Sub oscillator on / off | `shell-mpc/overlay/mixer_ui.h:322` |
| Route | `sub.destination` | Tap | Opens destination popup list (Filter 1, Filter 2, Filters 1+2, Effects only, Direct out, Filter blend, None) | `shell-mpc/overlay/mixer_ui.h:322` |
| LEVEL | `sub.level` | Drag vertical / Double-tap / Wheel / Q-Link 1 | Adjusts Sub oscillator output level (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:322` |
| PAN | `sub.pan` | Drag horizontal / Double-tap / Wheel / Q-Link 2 | Adjusts Sub stereo panning (L 100 % to R 100 %) | `shell-mpc/overlay/mixer_ui.h:322` |
| B1 | `sub.bus1_send` | Drag horizontal / Double-tap / Wheel / Q-Link 3 | Adjusts Sub send level to FX Bus 1 (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:322` |
| B2 | `sub.bus2_send` | Drag horizontal / Double-tap / Wheel / Q-Link 4 | Adjusts Sub send level to FX Bus 2 (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:322` |
| A | `osc1.on` | Tap | Toggles Oscillator A on / off | `shell-mpc/overlay/mixer_ui.h:323` |
| Route | `osc1.destination` | Tap | Opens destination popup list (Filter 1, Filter 2, Filters 1+2, Effects only, Direct out, Filter blend, None) | `shell-mpc/overlay/mixer_ui.h:323` |
| LEVEL | `osc1.level` | Drag vertical / Double-tap / Wheel / Q-Link 1 | Adjusts Oscillator A output level (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:323` |
| PAN | `osc1.pan` | Drag horizontal / Double-tap / Wheel / Q-Link 2 | Adjusts Oscillator A stereo panning (L 100 % to R 100 %) | `shell-mpc/overlay/mixer_ui.h:323` |
| B1 | `osc1.bus1_send` | Drag horizontal / Double-tap / Wheel / Q-Link 3 | Adjusts Oscillator A send level to FX Bus 1 (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:323` |
| B2 | `osc1.bus2_send` | Drag horizontal / Double-tap / Wheel / Q-Link 4 | Adjusts Oscillator A send level to FX Bus 2 (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:323` |
| B | `osc2.on` | Tap | Toggles Oscillator B on / off | `shell-mpc/overlay/mixer_ui.h:324` |
| Route | `osc2.destination` | Tap | Opens destination popup list (Filter 1, Filter 2, Filters 1+2, Effects only, Direct out, Filter blend, None) | `shell-mpc/overlay/mixer_ui.h:324` |
| LEVEL | `osc2.level` | Drag vertical / Double-tap / Wheel / Q-Link 1 | Adjusts Oscillator B output level (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:324` |
| PAN | `osc2.pan` | Drag horizontal / Double-tap / Wheel / Q-Link 2 | Adjusts Oscillator B stereo panning (L 100 % to R 100 %) | `shell-mpc/overlay/mixer_ui.h:324` |
| B1 | `osc2.bus1_send` | Drag horizontal / Double-tap / Wheel / Q-Link 3 | Adjusts Oscillator B send level to FX Bus 1 (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:324` |
| B2 | `osc2.bus2_send` | Drag horizontal / Double-tap / Wheel / Q-Link 4 | Adjusts Oscillator B send level to FX Bus 2 (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:324` |
| C | `osc3.on` | Tap | Toggles Oscillator C on / off | `shell-mpc/overlay/mixer_ui.h:325` |
| Route | `osc3.destination` | Tap | Opens destination popup list (Filter 1, Filter 2, Filters 1+2, Effects only, Direct out, Filter blend, None) | `shell-mpc/overlay/mixer_ui.h:325` |
| LEVEL | `osc3.level` | Drag vertical / Double-tap / Wheel / Q-Link 1 | Adjusts Oscillator C output level (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:325` |
| PAN | `osc3.pan` | Drag horizontal / Double-tap / Wheel / Q-Link 2 | Adjusts Oscillator C stereo panning (L 100 % to R 100 %) | `shell-mpc/overlay/mixer_ui.h:325` |
| B1 | `osc3.bus1_send` | Drag horizontal / Double-tap / Wheel / Q-Link 3 | Adjusts Oscillator C send level to FX Bus 1 (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:325` |
| B2 | `osc3.bus2_send` | Drag horizontal / Double-tap / Wheel / Q-Link 4 | Adjusts Oscillator C send level to FX Bus 2 (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:325` |
| NOISE | `noise.on` | Tap | Toggles Noise generator on / off | `shell-mpc/overlay/mixer_ui.h:326` |
| Route | `noise.destination` | Tap | Opens destination popup list (Filter 1, Filter 2, Filters 1+2, Effects only, Direct out, Filter blend, None) | `shell-mpc/overlay/mixer_ui.h:326` |
| LEVEL | `noise.level` | Drag vertical / Double-tap / Wheel / Q-Link 1 | Adjusts Noise generator output level (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:326` |
| PAN | `noise.pan` | Drag horizontal / Double-tap / Wheel / Q-Link 2 | Adjusts Noise generator stereo panning (L 100 % to R 100 %) | `shell-mpc/overlay/mixer_ui.h:326` |
| B1 | `noise.bus1_send` | Drag horizontal / Double-tap / Wheel / Q-Link 3 | Adjusts Noise send level to FX Bus 1 (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:326` |
| B2 | `noise.bus2_send` | Drag horizontal / Double-tap / Wheel / Q-Link 4 | Adjusts Noise send level to FX Bus 2 (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:326` |
| F1 | `filter1.on` | Tap | Toggles Filter 1 on / off | `shell-mpc/overlay/mixer_ui.h:327` |
| Route | `filter1.destination` | Tap | Opens destination popup list (Effects only, Direct out, None) | `shell-mpc/overlay/mixer_ui.h:327` |
| LEVEL | `filter1.level` | Drag vertical / Double-tap / Wheel / Q-Link 1 | Adjusts Filter 1 output level (-60 dB to +12 dB) | `shell-mpc/overlay/mixer_ui.h:327` |
| PAN | `filter1.pan` | Drag horizontal / Double-tap / Wheel / Q-Link 2 | Adjusts Filter 1 stereo panning (L 100 % to R 100 %) | `shell-mpc/overlay/mixer_ui.h:327` |
| B1 | `filter1.bus1_send` | Drag horizontal / Double-tap / Wheel / Q-Link 3 | Adjusts Filter 1 send level to FX Bus 1 (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:327` |
| B2 | `filter1.bus2_send` | Drag horizontal / Double-tap / Wheel / Q-Link 4 | Adjusts Filter 1 send level to FX Bus 2 (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:327` |
| F2 | `filter2.on` | Tap | Toggles Filter 2 on / off | `shell-mpc/overlay/mixer_ui.h:328` |
| Route | `filter2.destination` | Tap | Opens destination popup list (Effects only, Direct out, None) | `shell-mpc/overlay/mixer_ui.h:328` |
| LEVEL | `filter2.level` | Drag vertical / Double-tap / Wheel / Q-Link 1 | Adjusts Filter 2 output level (-60 dB to +12 dB) | `shell-mpc/overlay/mixer_ui.h:328` |
| PAN | `filter2.pan` | Drag horizontal / Double-tap / Wheel / Q-Link 2 | Adjusts Filter 2 stereo panning (L 100 % to R 100 %) | `shell-mpc/overlay/mixer_ui.h:328` |
| B1 | `filter2.bus1_send` | Drag horizontal / Double-tap / Wheel / Q-Link 3 | Adjusts Filter 2 send level to FX Bus 1 (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:328` |
| B2 | `filter2.bus2_send` | Drag horizontal / Double-tap / Wheel / Q-Link 4 | Adjusts Filter 2 send level to FX Bus 2 (0 to 100 %) | `shell-mpc/overlay/mixer_ui.h:328` |
| BUS1 | `bus1.fx_bypass` | Tap | Toggles Bus 1 FX bypass (on = active FX, off = bypassed) | `shell-mpc/overlay/mixer_ui.h:329` |
| Route | `bus1.destination` | Tap | Opens destination popup list (Main, Direct, Other bus) | `shell-mpc/overlay/mixer_ui.h:329` |
| LEVEL | `bus1.level` | Drag vertical / Double-tap / Wheel / Q-Link 1 | Adjusts FX Bus 1 return level (-60 dB to +6 dB, fine scale) | `shell-mpc/overlay/mixer_ui.h:329` |
| BUS2 | `bus2.fx_bypass` | Tap | Toggles Bus 2 FX bypass (on = active FX, off = bypassed) | `shell-mpc/overlay/mixer_ui.h:330` |
| Route | `bus2.destination` | Tap | Opens destination popup list (Main, Direct, Other bus) | `shell-mpc/overlay/mixer_ui.h:330` |
| LEVEL | `bus2.level` | Drag vertical / Double-tap / Wheel / Q-Link 1 | Adjusts FX Bus 2 return level (-60 dB to +6 dB, fine scale) | `shell-mpc/overlay/mixer_ui.h:330` |
| MASTER | `main.fx_bypass` | Tap | Toggles Main FX rack bypass (on = active FX, off = bypassed) | `shell-mpc/overlay/mixer_ui.h:331` |
| OUT | - | Tap | Disabled destination button; Master routes directly to audio output | `shell-mpc/overlay/mixer_ui.h:156` |
| LEVEL | `master.volume` | Drag vertical / Double-tap / Wheel / Q-Link 1 | Adjusts Master output volume (-60 dB to +6 dB, fine scale with peak hold) | `shell-mpc/overlay/mixer_ui.h:331` |

## Opens

Tapping any enabled destination route button (`destButton`, `shell-mpc/overlay/mixer_ui.h:381-401`) on strips 0 to 8 opens a modal floating list popup over the tab canvas:
- Destination selection popup (`ui::ListPopup list_`, page ID `kPageList = 25`, element ID `kList = 2000`, backdrop ID `kListBackdrop = 2001`, `shell-mpc/overlay/mixer_ui.h:38,45-46,381-401`):
  - Placed dynamically centered under the tapped destination button with width 260 px and height sized to row count (up to 6 visible rows of 44 px each plus padding, `shell-mpc/overlay/mixer_ui.h:392-395`).
  - Stack management: pushes detail page `kPageList` onto `UiStack` (`shell-mpc/overlay/mixer_ui.h:399`).
  - Items shown depending on the channel strip:
    - Strips 0 to 4 (`SUB`, `A`, `B`, `C`, `NOISE`): 7 choices (`Filter 1`, `Filter 2`, `Filters 1+2`, `Effects only`, `Direct out`, `Filter blend`, `None`).
    - Strips 5 to 6 (`F1`, `F2`): 3 choices (`Effects only`, `Direct out`, `None`).
    - Strips 7 to 8 (`BUS1`, `BUS2`): 3 choices (`Main`, `Direct`, `Other bus`).
  - Controls inside `ListPopup`:

| Label on screen | Parameter key | Gesture | Effect | Source |
| --- | --- | --- | --- | --- |
| Destination choice item | - | Tap | Selects routing destination for the strip, writes parameter value, closes popup, and calls `stack.back()` | `shell-mpc/overlay/mixer_ui.h:357` |
| Destination list scroll | - | Drag vertical / Wheel | Scrolls through available destination choices | `shell-mpc/overlay/mixer_ui.h:253` |
| Backdrop scrim | - | Tap outside popup | Dismisses popup without altering destination and calls `stack.back()` | `shell-mpc/overlay/mixer_ui.h:365` |

Note: Strip 9 (`MASTER`) destination button is disabled with label "OUT" (`bindings_[9].destParam = -1`, `shell-mpc/overlay/mixer_ui.h:156-157`), so tapping it opens nothing.

## Q-Link groups

The page defines 10 Q-Link groups, exactly one per channel strip (`MixerFullScreen::setupGroups()`, `shell-mpc/ui/mixer_screen.h:279-289`):
- Groups: Group 0 `SUB`, Group 1 `A`, Group 2 `B`, Group 3 `C`, Group 4 `NOISE`, Group 5 `F1`, Group 6 `F2`, Group 7 `BUS1`, Group 8 `BUS2`, Group 9 `MASTER`.
- Selection: Touching anywhere inside a channel strip activates that strip's Q group (`MixerFullScreen::touchDown`, `shell-mpc/ui/mixer_screen.h:192-205`, `MixerUi::touch`, `shell-mpc/overlay/mixer_ui.h:227`).
- Slot assignments (knobs 1 to 4 of the active group):
  - Slot 0: `LEVEL` fader (`shell-mpc/ui/mixer_screen.h:284`)
  - Slot 1: `PAN` slider (`shell-mpc/ui/mixer_screen.h:285`)
  - Slot 2: `B1` Send 1 slider (`shell-mpc/ui/mixer_screen.h:286`)
  - Slot 3: `B2` Send 2 slider (`shell-mpc/ui/mixer_screen.h:287`)
- Missing members omitted:
  - For `BUS1` (strip 7) and `BUS2` (strip 8), the engine has only `levelParam` (`bus1.level`, `bus2.level`); `panParam`, `send1Param`, and `send2Param` are -1 (`shell-mpc/overlay/mixer_ui.h:329-330`). Slots 1, 2, and 3 are set to `nullptr` via `setStripMembers` (`shell-mpc/overlay/mixer_ui.h:63-64`, `shell-mpc/ui/mixer_screen.h:186-189`). They display no index chips and are excluded from the Q-Link highlight frame.
  - For `MASTER` (strip 9), only `levelParam` (`master.volume`) exists. Slots 1, 2, and 3 are set to `nullptr`.
- Bank of 16 Q-Links (`QSlots::publish`, `shell-mpc/overlay/mixer_ui.h:340-348`):
  - On 16-knob hardware (MPC XL / wide device), 4 strips are mapped simultaneously:
    - Knobs 1 .. 4: Active selected strip (Block 1).
    - Knobs 5 .. 8: Next strip in left-to-right reading order (Block 2, wrapping cyclically).
    - Knobs 9 .. 12: Second next strip (Block 3).
    - Knobs 13 .. 16: Third next strip (Block 4).
  - Strip index chips display absolute knob numbers (1 to 16) above each control (`shell-mpc/ui/mixer_screen.h:237`).

## Live parts

The page dynamically animates and updates multiple graphical elements every frame:
1. Integrated Peak Level Meters (`ui::Fader::drawBody`, `shell-mpc/ui/mixer_strip.h:176-217`):
   - Every frame in `MixerUi::update()` (`shell-mpc/overlay/mixer_ui.h:93,183-200`), the page calls `inst.requestMeters()` to read `in.live->stripPeak[s]` for all 10 channel strips.
   - Strip peak meters render a colored gradient bar beside each fader track: cyan (`theme::L().flow[0]`) at -60 dBFS rising through blue, violet, and magenta (`flow[3]`) near 0 dBFS, turning bright red (`theme::L().negative`) when clipping (> 1.0) (`shell-mpc/ui/mixer_strip.h:196-200`).
   - A crisp white peak line is drawn at the crest of the meter bar (`shell-mpc/ui/mixer_strip.h:204-206`).
2. Master Output Peak Hold (`shell-mpc/overlay/mixer_ui.h:190-195`, `shell-mpc/ui/mixer_strip.h:209-216`):
   - For Strip 9 (`MASTER`), `fader.state.hasPeakHold = true`. It tracks the highest signal peak and decays slowly at `0.5f * dt` (matching the Master page output meter). Rendered as a distinct 2 px bar across the meter slot.
3. Signal Flow Routing Lines (`MixerRoutingBar::drawBody`, `shell-mpc/ui/mixer_screen.h:42-118`):
   - The top routing bar continuously redraws illuminated signal path lines connecting the currently selected channel strip's bottom connection point up to its destination node chip (`FLT 1`, `FLT 2`, `MAIN FX`, `DIRECT`, `MASTER`) or across to a neighbouring bus strip.
   - The destination chip corresponding to the selected channel's route is illuminated with bright accent ink (`theme::L().flow[0]`), while inactive target chips remain faint (`shell-mpc/ui/mixer_screen.h:92-96`).
4. Real-time Parameter Value Follower:
   - Fader thumb positions, slider thumbs, and value readout strings update continuously in response to host automation, MIDI control, and modulation.

## Duplicates

42 of the 50 parameter keys controlled on the Mixer page are direct duplicates of controls on other overlay pages or tabs:

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
     - Also on Filter page (`shell-mpc/overlay/filter_ui.h:275-276,302,327`): Filter 1 power toggle (full screen power), output level (full screen level in OUT group and route popup level), stereo pan (full screen pan and route popup pan), destination (route popup destination), and bus sends (route popup bus1 and bus2).
   - `filter2.on`, `filter2.level`, `filter2.pan`, `filter2.destination`, `filter2.bus1_send`, `filter2.bus2_send`:
     - Also on Filter page (`shell-mpc/overlay/filter_ui.h:275-276,302,327`): Filter 2 power toggle, output level, stereo pan, destination, and bus sends.

3. Master Channel (Strip 9):
   - `master.volume`:
     - Also on Master page (`shell-mpc/overlay/master_ui.h:240,480`): Volume knob in the OUTPUT group.

4. Parameters Unique to the Mixer Page (No duplicates):
   - `bus1.fx_bypass`, `bus1.level`, `bus1.destination` (Strip 7 `BUS1`): only accessible on the Mixer page.
   - `bus2.fx_bypass`, `bus2.level`, `bus2.destination` (Strip 8 `BUS2`): only accessible on the Mixer page.
   - `main.fx_bypass` (Strip 9 `MASTER`): only accessible on the Mixer page.

## Questionable

Judgements based on code inspection:

1. Non-functional, disabled "OUT" destination button on Master strip (`shell-mpc/overlay/mixer_ui.h:156-157`):
   - In `shell-mpc/overlay/mixer_ui.h:156-157`, `bindings_[9].destParam` is -1, so `strip.destButton.setLabel("OUT")` and `strip.destButton.setEnabled(false)` are executed. The button occupies a full 56 px tall touch cell in the MASTER strip layout (`shell-mpc/ui/mixer_strip.h:472`), styled identically to the interactive destination dropdowns of other strips. Tapping it does nothing. This is dead visual clutter that misleads the user into expecting an interactive routing choice.
2. Inverted FX bypass disguised as channel power switches (`shell-mpc/overlay/mixer_ui.h:57,104,329-331,379`):
   - The power toggle button on strips 0 to 6 controls actual channel power (`.on` parameters: 1 = active, 0 = off).
   - However, on strips 7 (`BUS1`), 8 (`BUS2`), and 9 (`MASTER`), the power toggle is bound to `bus1.fx_bypass`, `bus2.fx_bypass`, and `main.fx_bypass` with `powerInverted = true` (`shell-mpc/overlay/mixer_ui.h:329-331`). When the power icon is lit ("on"), bypass is 0 (effects enabled); when the power icon is dim ("off"), bypass is 1 (effects bypassed).
   - Tapping the "power" button of the MASTER channel does not mute or disable the synthesizer's master output—it merely bypasses the Main FX rack. This violates user expectation of a mixer master strip power/mute switch.
3. Severe layout asymmetry between source strips and bus/master strips (`shell-mpc/ui/mixer_strip.h:477-498`):
   - Strips 0 to 6 cram a fader (height ~190 px) and three 56 px slider cells (`pan`, `send1`, `send2`) into the strip.
   - Strips 7 to 9 completely omit pan and sends, so their faders stretch to the entire height of the strip (~390 px, `shell-mpc/ui/mixer_strip.h:493`).
   - This gives secondary bus returns and master volume double the physical fader travel and precision of the 7 primary sound sources (oscillators and filters), where fine level balancing is most critical.
4. Non-interactive signal flow routing bar (`shell-mpc/ui/mixer_screen.h:18-119`):
   - The top routing bar displays prominent target chips (`FLT 1`, `FLT 2`, `MAIN FX`, `DIRECT`, `MASTER`). Because they look like selectable chip buttons, users naturally attempt to tap them to re-route signals.
   - However, `MixerRoutingBar` implements no touch handler (`shell-mpc/ui/mixer_screen.h:18`); it is strictly passive display graphics. Users must instead tap the small `destButton` on each individual channel strip below.
5. "None" destination choice creates a silent dead end (`docs/params.json`, `shell-mpc/ui/mixer_screen.h:116-117`):
   - All oscillator and filter destination choice lists include `None` (`docs/params.json`). When selected, audio is simply dropped. In `MixerRoutingBar::drawBody` (`shell-mpc/ui/mixer_screen.h:116-117`), selecting `None` draws an awkward 16 px horizontal dead-end stub line. Since every strip already has a dedicated power toggle to silence it, having `None` as a routing target is confusing and redundant.
6. Extreme parameter redundancy across tabs (`shell-mpc/overlay/mixer_ui.h:322-328` vs `shell-mpc/overlay/osc_ui.h` and `shell-mpc/overlay/filter_ui.h`):
   - 84% of all controls on the Mixer page (42 out of 50 parameters) are identical duplicates of controls on the dedicated Osc, Filter, and Master tabs. Changing a send or destination can be done either in a channel strip or deep inside a popup modal on the source tab, causing confusion about which screen is the primary mixing location.
