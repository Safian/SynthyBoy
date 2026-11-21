# UI Map: Filter Page (`filterui`)

## Purpose

The Filter page is the MPC overlay skin page for the "Filter 1" and "Filter 2" tabs (`shell-mpc/overlay/overlay_layout.json:47-119`, `scripts/overlay-skin/mpc-ui-pages-overlay.json:66-79`).
It represents the view kind `filterui` (`kKindFilterUi = 6`, `shell-mpc/overlay/overlay_layout.h:10`, registered as stack element `FilterUi::kElementFilter = 4` in `shell-mpc/overlay/filter_ui.h:46`).
It is implemented by `FilterUi` (`shell-mpc/overlay/filter_ui.h:42-987`) using `FilterFullScreen` (`shell-mpc/ui/filter_display.h:760-1031`), `FilterDisplay` (`shell-mpc/ui/filter_display.h:33-545`), and `FilterRoutePanel` (`shell-mpc/ui/filter_display.h:550-664`), and instantiated as `PageView<FilterUi>` (`shell-mpc/overlay/pages.h:28,58`).

The page occupies the full MPC overlay tab canvas (1280 x 628 pixels) and provides complete control over the two multimode filters of SynthyBoy (Filter 1 and Filter 2), as well as dual-filter routing (`filter.routing`).
When opened, the page initializes to the filter indicated by the host tab (`in.instance = 1` on the "Filter 1" tab, `in.instance = 2` on the "Filter 2" tab; `shell-mpc/overlay/filter_ui.h:95`), but provides an in-page 3-way CURVES selector (`instances`, displaying `F1`, `F2`, `F1+F2`; `shell-mpc/ui/filter_display.h:769, 796-799`) that allows viewing and editing either filter, or inspecting both simultaneously with resultant curve response on either tab.

Key functional capabilities:
1. **Filter Engine Models**: 27 selectable filter models (`synthyboy::filters::filterModelCount() == 27`, Analog, Dirty, Ladder, Digital, Diode, Formant, Comb, Phaser, Multi SVF, Slope, EQ 6 dB, Ring Mod, Sample Hold, Smear, French LP, Add Bass, Scream, New Wsp, DJ Mixer, Diffusor, EMS Ladder, PZ SVF, Normal, Multi Morph, Flanges, Misc, New; `shell-mpc/overlay/tests/filter_ui_host.cpp:315`).
2. **Dynamic Model Styles**: Each model provides 1 to 12 distinct styles (e.g. Diode offers 2 styles, Analog offers 6 styles, Multi SVF offers 12 styles; `shell-mpc/overlay/tests/filter_ui_host.cpp:917-988`). Under the "One-Style rule", models with only 1 style display the style selector dimmed and non-listable (`shell-mpc/overlay/filter_ui.h:543, 852-860`). Switching models automatically clamps out-of-range style indices (`shell-mpc/overlay/filter_ui.h:457-463, 504-508, 834-844`).
3. **Dedicated Model Modes**:
   - **Formant Mode** (Model 5, `kModelFormant`): The main response curve switches to an interactive 2D vowel XY pad (vowel targets I, E, A, O, U), while the 5-knob FORMANT block (`formantX`, `formantY`, `formantTrans`, `formantSpread`, `formantRes`) is revealed on the right (`shell-mpc/ui/filter_display.h:340-391, 836-844`). `var` and `clean` are disabled.
   - **Comb Mode** (Model 6, `kModelComb`): Enables the COMB offset knob (`filterN.comb_offset`, 0 to 84 semitones; `shell-mpc/overlay/filter_ui.h:878`, `shell-mpc/ui/filter_display.h:779`). `clean` is disabled.
   - **Clean Mode**: Toggles linear phase / oversampled clean operation on analog and ladder models (`filterN.clean_mode`; `shell-mpc/overlay/filter_ui.h:880`).
4. **Dual Response and Routing**: Supports Parallel, Series 1 to 2, and Series 2 to 1 routing (`filter.routing`; `shell-mpc/ui/filter_display.h:783`). In dual mode (`F1+F2`), the display computes and renders three simultaneous curves: Filter 1 (cyan), Filter 2 (magenta), and the resultant combination (white, 2 px stroke), with independent draggable interactive handles for both filters (`shell-mpc/overlay/filter_ui.h:651-688`, `shell-mpc/ui/filter_display.h:245-262, 293-319`).
5. **Detail Popups and Pages**: Opens the modal OUTPUT routing panel (`FilterRoutePanel`), the model/style selection popup (`ListPopup`), and full-screen parameter modulation inspection/editing (`ModulationsPage`).

## Layout

The Filter page occupies the entire tab region `(0, 110, 1280, 628)` (`shell-mpc/overlay/filter_ui.h:65, 74-75`, `shell-mpc/ui/filter_display.h:849`).
The layout consists of three primary vertical sections: Top Bar, Display / Formant Row, and Bottom Parameter Blocks.

```
+----------------------------------------------------------------------------------------------------------------------------------+ y=110
| [POWER]  [CURVES: F1 | F2 | F1+F2]        [MODEL: Analog v][v]  [STYLE: 12 dB v]       [CLEAN]                     [OUTPUT: M]   | Top Bar (h=56)
+----------------------------------------------------------------------------------------------------------------------------------+ y=178
|                                                                                                                                  |
|                                       FREQUENCY RESPONSE DISPLAY (20 Hz - 20 kHz, -48 to +30 dB)                                 | Display Row
|                                (or FORMANT XY PAD [I, E, A, O, U] + FORMANT BLOCK [X, Y, TRANS, SPREAD, RESO] on right)          | (h=234)
|                                                                                                                                  |
+---------------------------------------------+---------------------------------------+--------------------------------------------+ y=420
| MAIN                                        | TRACK                                 | OUT                                        |
|  [CUTOFF]   [RESO]   [DRIVE]   [MIX]        |  [KEYTRACK]  [ENV 1]  [VAR]  [COMB]   |  [LEVEL]   [PAN]   [BLEND]   [ROUTING]     | Bottom Band
|                                             |                                       |                              Parallel      | (h=214)
|                                             |                                       |                              Series 1->2   |
|                                             |                                       |                              Series 2->1   |
+---------------------------------------------+---------------------------------------+--------------------------------------------+ y=634
```

### 1. Top Bar (`shell-mpc/ui/filter_display.h:901-915`)
Located at `(rect.x + 8, rect.y + 8, 1264, 56)`. Uses an 8-slot row layout (`layoutRow`):
- `close` MenuButton (`shell-mpc/ui/filter_display.h:767, 905`): Width 0 / hidden in page mode (`setPageMode(true)` at `shell-mpc/overlay/filter_ui.h:69`, `shell-mpc/ui/filter_display.h:830`).
- `power` ToggleIcon (`shell-mpc/ui/filter_display.h:768, 791, 906`): Width 56 px, height 56 px. Displays power symbol icon; toggles active filter on/off (`filterN.on`).
- `instances` LedList ("CURVES", `shell-mpc/ui/filter_display.h:765, 769, 796-799, 907-908`): Fixed width 364 px (`kCurvesCaptionW = 76` + 3 cells of 96 px). Labelled "CURVES" on the left, with three horizontal cells `F1`, `F2`, and `F1+F2`. Selects active edit filter and curve display mode.
- `model` Stepper (`shell-mpc/ui/filter_display.h:770, 800, 909`): Flexible width (weight 5, ~265 px), height 56 px. Displays active filter model name with left/right stepping chevrons. Tapping the center opens `ListPopup`.
- `modelList` MenuButton (`shell-mpc/ui/filter_display.h:771, 910`): Width 56 px, height 56 px. Contains downward chevron icon; opens 27-model `ListPopup`.
- `style` Stepper (`shell-mpc/ui/filter_display.h:770, 801, 911`): Flexible width (weight 4, ~212 px), height 56 px. Displays active style name with stepping chevrons. Under the One-Style rule, when the model has 1 style, it appears dimmed and non-listable.
- `clean` LedToggle (`shell-mpc/ui/filter_display.h:772, 793, 912`): Width 72 px, height 56 px. Displays LED dot and "CLEAN" label. Toggles 2x oversampled clean mode (`filterN.clean_mode`). Disabled and dimmed for Comb and Formant models.
- `route` RouteButton ("OUTPUT", `shell-mpc/ui/filter_display.h:773, 889-891, 913-914`): Width 134 px total (70 px "OUTPUT" label + 64 x 64 px rounded route badge). Displays destination target letter ("M" Main FX, "D" Direct out, "-" None). Tapping opens `FilterRoutePanel`.

### 2. Display Row (`shell-mpc/ui/filter_display.h:955-973`)
Located between the top bar and bottom parameter blocks at `y = 178`, height `234 px`:
- **Standard Mode (All non-Formant models)** (`shell-mpc/ui/filter_display.h:970-973`):
  - `display` (`FilterDisplay`, `shell-mpc/ui/filter_display.h:33-545`): Full width `(x + 8, y = 178, w = 1264, h = 234)`.
  - Logarithmic frequency axis from 20 Hz to 20 kHz with decade grid lines at 50, 100, 200, 500, 1k, 2k, 5k, 10k, 20 kHz.
  - Decibel axis from -48 dB to +30 dB with horizontal grid lines at -48, -24, 0, and +24 dB.
  - Single mode (`F1` or `F2`): 256-point real impulse response curve (cyan for F1, magenta for F2) with semi-transparent fill (alpha 34) down to -48 dB. When the filter is switched off, the curve is drawn dim (alpha 0.7 stroke, 0 fill) with the label "FILTER N OFF".
  - Dual mode (`F1+F2`): Displays three curves: F1 curve (cyan, alpha 0.6), F2 curve (magenta, alpha 0.6), and resultant combined curve (white, 2 px stroke, alpha 1.0). Top-left legend shows "F1", "F2", "F1+F2".
  - Interactive Puck Handles: 4 px solid disc with 8.5 px outer ring positioned directly on the curve at `(cutoff, resonance)`. In dual mode, both F1 (cyan) and F2 (magenta) handles are displayed and can be grabbed independently. While dragging, a floating tooltip bubble displays frequency (e.g. "1.80 kHz") and resonance percentage.
  - Sounding Voice Cutoff Markers: Up to 8 vertical lines showing real-time key-tracked cutoff positions for active sounding voices (`live->activeVoices`).
  - Modulated Cutoff Marker: Bright vertical line in `theme.flow[1]` showing the live modulated cutoff of the heard voice.
- **Formant Mode (Model 5 Formant)** (`shell-mpc/ui/filter_display.h:956-968`):
  - `display` shrinks to width 696 px `(x + 8, y = 178, w = 696, h = 234)`.
  - Renders Formant XY Pad (`shell-mpc/ui/filter_display.h:340-391`): Center crosshairs, 5 circular vowel targets with labels ("I", "E", "A", "O", "U"), modulated live formant puck (`liveFormantX`, `liveFormantY`), and draggable interactive handle puck displaying floating tooltip "X xx% Y yy%".
  - `formantBlock_` sits on the right `(x = 712, y = 178, w = 560, h = 234)`: Contains 5 controls:
    - 2x2 grid of medium knobs on left: `formantX` ("X"), `formantY` ("Y"), `formantTrans` ("TRANSPOSE"), `formantSpread` ("SPREAD").
    - Full-height knob on right: `formantRes` ("RESONANCE").

### 3. Bottom Band (`shell-mpc/ui/filter_display.h:917-952`)
Located at `y = 420` to `y = 634`, height `214 px` (`kBandH = 214`). Divided into three titled blocks side-by-side, each mapped to a dedicated Q-Link encoder group (Q1..Q4):
- **MAIN Block** (`mainBlock_`, x = 4, y = 420, w ~ 450 px; `shell-mpc/ui/filter_display.h:922, 949`):
  - 4 equal knob slots: `cutoff` ("CUTOFF"), `resonance` ("RESO"), `drive` ("DRIVE"), `mix` ("MIX").
- **TRACK Block** (`trackBlock_`, x ~ 458, y = 420, w ~ 398 px; `shell-mpc/ui/filter_display.h:923, 950`):
  - 4 slots: `keytrack` ("KEYTRACK"), `env1` ("ENV 1" LED toggle, width 96 px, height 56 px centered in slot), `var` ("VAR", disabled in Formant mode), `combOffset` ("COMB", enabled only in Comb model, disabled and dim otherwise).
- **OUT Block** (`outBlock_`, x ~ 860, y = 420, w ~ 416 px; `shell-mpc/ui/filter_display.h:924, 951`):
  - 4 slots: `level` ("LEVEL"), `pan` ("PAN", bipolar, displays "C", "L xx%", "R xx%"), `blend` ("BLEND"), `routing` ("ROUTING" 3-row LED list, width 150 px: "Parallel", "Series 1 to 2", "Series 2 to 1").

## Controls

| Label on screen | Parameter key | Gesture | Effect | Source |
|---|---|---|---|---|
| CLOSE | - | Tap | Closes full-screen filter page (hidden in page mode) | `shell-mpc/overlay/filter_ui.h:253` |
| POWER | `filter1.on` | Tap toggle / Q1 turn | Toggles Filter 1 power on / off | `shell-mpc/overlay/filter_ui.h:254` |
| CURVES | - | Tap cell (F1 / F2 / F1+F2) / Wheel | Selects which filter curve(s) to draw on display and sets active edit filter | `shell-mpc/overlay/filter_ui.h:255` |
| MODEL | `filter1.model` | Tap chevron / Wheel / Q turn | Steps through 27 filter models for Filter 1 | `shell-mpc/overlay/filter_ui.h:256` |
| MODEL (List) | `filter1.model` | Tap center / Tap list button | Opens popup list of all 27 filter models for Filter 1 | `shell-mpc/overlay/filter_ui.h:257` |
| STYLE | `filter1.style` | Tap chevron / Wheel / Q turn | Steps through filter styles for current model of Filter 1 (dim/disabled if model has 1 style) | `shell-mpc/overlay/filter_ui.h:258` |
| CLEAN | `filter1.clean_mode` | Tap toggle | Toggles 2x oversampling clean mode for Filter 1 (disabled for Comb and Formant) | `shell-mpc/overlay/filter_ui.h:259` |
| OUTPUT | - | Tap | Opens FilterRoutePanel popup for active filter | `shell-mpc/overlay/filter_ui.h:260` |
| Display Cutoff | `filter1.cutoff` | Drag horizontal / Touch down | Sets Filter 1 cutoff frequency on log scale (20 Hz to 20 kHz) | `shell-mpc/overlay/filter_ui.h:261` |
| Display Resonance | `filter1.resonance` | Drag vertical / Touch down | Sets Filter 1 resonance percentage (0% to 100%) | `shell-mpc/overlay/filter_ui.h:261` |
| Display Formant X | `filter1.formant.x` | Drag horizontal / Touch down | Sets Filter 1 Formant X position (0% to 100%) on XY pad | `shell-mpc/overlay/filter_ui.h:261` |
| Display Formant Y | `filter1.formant.y` | Drag vertical / Touch down | Sets Filter 1 Formant Y position (0% to 100%) on XY pad | `shell-mpc/overlay/filter_ui.h:261` |
| CUTOFF | `filter1.cutoff` | Rotary drag / Wheel / Q1 turn / Long press (Mods) | Adjusts Filter 1 cutoff frequency (20 Hz to 20 kHz) | `shell-mpc/overlay/filter_ui.h:263` |
| RESO | `filter1.resonance` | Rotary drag / Wheel / Q2 turn / Long press (Mods) | Adjusts Filter 1 resonance (0% to 100%) | `shell-mpc/overlay/filter_ui.h:264` |
| DRIVE | `filter1.drive` | Rotary drag / Wheel / Q3 turn / Long press (Mods) | Adjusts Filter 1 drive (0 dB to 20 dB) | `shell-mpc/overlay/filter_ui.h:265` |
| MIX | `filter1.mix` | Rotary drag / Wheel / Q4 turn / Long press (Mods) | Adjusts Filter 1 dry/wet mix (0% to 100%) | `shell-mpc/overlay/filter_ui.h:266` |
| KEYTRACK | `filter1.keytrack` | Rotary drag / Wheel / Q1 turn / Long press (Mods) | Adjusts Filter 1 key tracking (-100% to +100%, bipolar) | `shell-mpc/overlay/filter_ui.h:268` |
| ENV 1 | `filter1.env1` | Tap toggle / Q2 turn | Toggles Filter 1 envelope 1 modulation on / off | `shell-mpc/overlay/filter_ui.h:269` |
| VAR | `filter1.var` | Rotary drag / Wheel / Q3 turn / Long press (Mods) | Adjusts Filter 1 variation spread (0% to 100%; disabled in Formant mode) | `shell-mpc/overlay/filter_ui.h:270` |
| COMB | `filter1.comb_offset` | Rotary drag / Wheel / Q4 turn / Long press (Mods) | Adjusts Filter 1 comb blend offset (0 to 84 semitones; enabled only in Comb model) | `shell-mpc/overlay/filter_ui.h:271` |
| LEVEL | `filter1.level` | Rotary drag / Wheel / Q1 turn / Long press (Mods) | Adjusts Filter 1 output level (-60 dB to +12 dB) | `shell-mpc/overlay/filter_ui.h:273` |
| PAN | `filter1.pan` | Rotary drag / Wheel / Q2 turn / Long press (Mods) | Adjusts Filter 1 stereo panning (L 100% to R 100%, bipolar) | `shell-mpc/overlay/filter_ui.h:274` |
| BLEND | `filter1.blend` | Rotary drag / Wheel / Q3 turn / Long press (Mods) | Adjusts Filter 1 blend (0% to 100%) | `shell-mpc/overlay/filter_ui.h:275` |
| ROUTING | `filter.routing` | Tap row / Q4 turn | Sets dual filter routing (Parallel, Series 1 to 2, Series 2 to 1) | `shell-mpc/overlay/filter_ui.h:276` |
| X | `filter1.formant.x` | Rotary drag / Wheel / Q1 turn / Long press (Mods) | Adjusts Filter 1 Formant X position (0% to 100%; Formant model only) | `shell-mpc/overlay/filter_ui.h:278` |
| Y | `filter1.formant.y` | Rotary drag / Wheel / Q2 turn / Long press (Mods) | Adjusts Filter 1 Formant Y position (0% to 100%; Formant model only) | `shell-mpc/overlay/filter_ui.h:279` |
| TRANSPOSE | `filter1.formant.transpose` | Rotary drag / Wheel / Q3 turn / Long press (Mods) | Adjusts Filter 1 Formant vowel transpose (-12 to +12 semitones, bipolar; Formant model only) | `shell-mpc/overlay/filter_ui.h:280` |
| SPREAD | `filter1.formant.spread` | Rotary drag / Wheel / Q4 turn / Long press (Mods) | Adjusts Filter 1 Formant vowel stereo spread (-100% to +100%, bipolar; Formant model only) | `shell-mpc/overlay/filter_ui.h:282` |
| RESONANCE | `filter1.formant.resonance` | Rotary drag / Wheel / Long press (Mods) | Adjusts Filter 1 Formant peak resonance (30% to 100%; Formant model only) | `shell-mpc/overlay/filter_ui.h:281` |
| POWER (F2) | `filter2.on` | Tap toggle / Q1 turn | Toggles Filter 2 power on / off (when F2 is active) | `shell-mpc/overlay/filter_ui.h:254` |
| MODEL (F2) | `filter2.model` | Tap chevron / Wheel / Q turn | Steps through 27 filter models for Filter 2 (when F2 is active) | `shell-mpc/overlay/filter_ui.h:256` |
| MODEL (List F2) | `filter2.model` | Tap center / Tap list button | Opens popup list of all 27 filter models for Filter 2 | `shell-mpc/overlay/filter_ui.h:257` |
| STYLE (F2) | `filter2.style` | Tap chevron / Wheel / Q turn | Steps through filter styles for current model of Filter 2 (when F2 is active) | `shell-mpc/overlay/filter_ui.h:258` |
| CLEAN (F2) | `filter2.clean_mode` | Tap toggle | Toggles 2x oversampling clean mode for Filter 2 (when F2 is active) | `shell-mpc/overlay/filter_ui.h:259` |
| Display Cutoff (F2) | `filter2.cutoff` | Drag horizontal / Touch down | Sets Filter 2 cutoff frequency on log scale (20 Hz to 20 kHz; F2 handle in dual mode or when F2 is active) | `shell-mpc/overlay/filter_ui.h:261` |
| Display Resonance (F2) | `filter2.resonance` | Drag vertical / Touch down | Sets Filter 2 resonance percentage (0% to 100%; F2 handle in dual mode or when F2 is active) | `shell-mpc/overlay/filter_ui.h:261` |
| Display Formant X (F2) | `filter2.formant.x` | Drag horizontal / Touch down | Sets Filter 2 Formant X position (0% to 100%) on XY pad (when F2 is active) | `shell-mpc/overlay/filter_ui.h:261` |
| Display Formant Y (F2) | `filter2.formant.y` | Drag vertical / Touch down | Sets Filter 2 Formant Y position (0% to 100%) on XY pad (when F2 is active) | `shell-mpc/overlay/filter_ui.h:261` |
| CUTOFF (F2) | `filter2.cutoff` | Rotary drag / Wheel / Q1 turn / Long press (Mods) | Adjusts Filter 2 cutoff frequency (20 Hz to 20 kHz; when F2 is active) | `shell-mpc/overlay/filter_ui.h:263` |
| RESO (F2) | `filter2.resonance` | Rotary drag / Wheel / Q2 turn / Long press (Mods) | Adjusts Filter 2 resonance (0% to 100%; when F2 is active) | `shell-mpc/overlay/filter_ui.h:264` |
| DRIVE (F2) | `filter2.drive` | Rotary drag / Wheel / Q3 turn / Long press (Mods) | Adjusts Filter 2 drive (0 dB to 20 dB; when F2 is active) | `shell-mpc/overlay/filter_ui.h:265` |
| MIX (F2) | `filter2.mix` | Rotary drag / Wheel / Q4 turn / Long press (Mods) | Adjusts Filter 2 dry/wet mix (0% to 100%; when F2 is active) | `shell-mpc/overlay/filter_ui.h:266` |
| KEYTRACK (F2) | `filter2.keytrack` | Rotary drag / Wheel / Q1 turn / Long press (Mods) | Adjusts Filter 2 key tracking (-100% to +100%, bipolar; when F2 is active) | `shell-mpc/overlay/filter_ui.h:268` |
| ENV 1 (F2) | `filter2.env1` | Tap toggle / Q2 turn | Toggles Filter 2 envelope 1 modulation on / off (when F2 is active) | `shell-mpc/overlay/filter_ui.h:269` |
| VAR (F2) | `filter2.var` | Rotary drag / Wheel / Q3 turn / Long press (Mods) | Adjusts Filter 2 variation spread (0% to 100%; when F2 is active) | `shell-mpc/overlay/filter_ui.h:270` |
| COMB (F2) | `filter2.comb_offset` | Rotary drag / Wheel / Q4 turn / Long press (Mods) | Adjusts Filter 2 comb blend offset (0 to 84 semitones; when F2 is active) | `shell-mpc/overlay/filter_ui.h:271` |
| LEVEL (F2) | `filter2.level` | Rotary drag / Wheel / Q1 turn / Long press (Mods) | Adjusts Filter 2 output level (-60 dB to +12 dB; when F2 is active) | `shell-mpc/overlay/filter_ui.h:273` |
| PAN (F2) | `filter2.pan` | Rotary drag / Wheel / Q2 turn / Long press (Mods) | Adjusts Filter 2 stereo panning (L 100% to R 100%, bipolar; when F2 is active) | `shell-mpc/overlay/filter_ui.h:274` |
| BLEND (F2) | `filter2.blend` | Rotary drag / Wheel / Q3 turn / Long press (Mods) | Adjusts Filter 2 blend (0% to 100%; when F2 is active) | `shell-mpc/overlay/filter_ui.h:275` |
| X (F2) | `filter2.formant.x` | Rotary drag / Wheel / Q1 turn / Long press (Mods) | Adjusts Filter 2 Formant X position (0% to 100%; when F2 is active) | `shell-mpc/overlay/filter_ui.h:278` |
| Y (F2) | `filter2.formant.y` | Rotary drag / Wheel / Q2 turn / Long press (Mods) | Adjusts Filter 2 Formant Y position (0% to 100%; when F2 is active) | `shell-mpc/overlay/filter_ui.h:279` |
| TRANSPOSE (F2) | `filter2.formant.transpose` | Rotary drag / Wheel / Q3 turn / Long press (Mods) | Adjusts Filter 2 Formant vowel transpose (-12 to +12 semitones, bipolar; when F2 is active) | `shell-mpc/overlay/filter_ui.h:280` |
| SPREAD (F2) | `filter2.formant.spread` | Rotary drag / Wheel / Q4 turn / Long press (Mods) | Adjusts Filter 2 Formant vowel stereo spread (-100% to +100%, bipolar; when F2 is active) | `shell-mpc/overlay/filter_ui.h:282` |
| RESONANCE (F2) | `filter2.formant.resonance` | Rotary drag / Wheel / Long press (Mods) | Adjusts Filter 2 Formant peak resonance (30% to 100%; when F2 is active) | `shell-mpc/overlay/filter_ui.h:281` |

## Opens

### 1. Output Routing Popup (`FilterRoutePanel`, `kPageRoute = 20`, `shell-mpc/overlay/filter_ui.h:48, 76-80, 284-289, 425`, `shell-mpc/ui/filter_display.h:550-664`)
- **Opened by**: Tapping the `route` button (labelled "OUTPUT") in the top bar (`shell-mpc/overlay/filter_ui.h:425`).
- **Placement**: Centered modal popup at `(w / 2 - 260, kTabY + kTabH / 2 - 120, 520, 240)` with dimmed semi-transparent background overlay covering the entire tab canvas (`shell-mpc/overlay/filter_ui.h:76-78`, `shell-mpc/ui/filter_display.h:593-599`).
- **Content**: Configures audio routing destination, bus send levels, and master level/pan for the active filter.
- **Controls inside popup**:

| Label on screen | Parameter key | Gesture | Effect | Source |
|---|---|---|---|---|
| DESTINATION | `filter1.destination` | Tap option / Q1 turn | Sets Filter 1 routing target (Effects only, Direct out, None) | `shell-mpc/overlay/filter_ui.h:284` |
| BUS 1 | `filter1.bus1_send` | Rotary drag / Wheel / Q2 turn | Adjusts Filter 1 FX Bus 1 send level (0% to 100%) | `shell-mpc/overlay/filter_ui.h:285` |
| BUS 2 | `filter1.bus2_send` | Rotary drag / Wheel / Q3 turn | Adjusts Filter 1 FX Bus 2 send level (0% to 100%) | `shell-mpc/overlay/filter_ui.h:286` |
| LEVEL | `filter1.level` | Rotary drag / Wheel / Q4 turn | Adjusts Filter 1 output level (-60 dB to +12 dB) | `shell-mpc/overlay/filter_ui.h:287` |
| PAN | `filter1.pan` | Rotary drag / Wheel | Adjusts Filter 1 stereo panning (L 100% to R 100%, bipolar) | `shell-mpc/overlay/filter_ui.h:288` |
| DESTINATION (F2) | `filter2.destination` | Tap option / Q1 turn | Sets Filter 2 routing target (Effects only, Direct out, None; when F2 is active) | `shell-mpc/overlay/filter_ui.h:284` |
| BUS 1 (F2) | `filter2.bus1_send` | Rotary drag / Wheel / Q2 turn | Adjusts Filter 2 FX Bus 1 send level (0% to 100%; when F2 is active) | `shell-mpc/overlay/filter_ui.h:285` |
| BUS 2 (F2) | `filter2.bus2_send` | Rotary drag / Wheel / Q3 turn | Adjusts Filter 2 FX Bus 2 send level (0% to 100%; when F2 is active) | `shell-mpc/overlay/filter_ui.h:286` |
| LEVEL (F2) | `filter2.level` | Rotary drag / Wheel / Q4 turn | Adjusts Filter 2 output level (-60 dB to +12 dB; when F2 is active) | `shell-mpc/overlay/filter_ui.h:287` |
| PAN (F2) | `filter2.pan` | Rotary drag / Wheel | Adjusts Filter 2 stereo panning (L 100% to R 100%, bipolar; when F2 is active) | `shell-mpc/overlay/filter_ui.h:288` |
| CLOSE | - | Tap | Dismisses Output routing popup | `shell-mpc/overlay/filter_ui.h:289` |
| Backdrop | - | Tap outside box | Dismisses Output routing popup | `shell-mpc/overlay/filter_ui.h:79` |

### 2. Model / Style Selection List (`ListPopup`, `kPageList = 21`, `shell-mpc/overlay/filter_ui.h:48, 81-84, 447-469, 517-560`)
- **Opened by**:
  - Tapping center of `model` Stepper or tapping `modelList` MenuButton (`shell-mpc/overlay/filter_ui.h:427, 532`): opens list of 27 filter models for the active filter.
  - Tapping center of `style` Stepper (`shell-mpc/overlay/filter_ui.h:428, 532`) when the active model has > 1 style: opens dynamically sized popup of styles for that model.
- **Placement**: Standard popup box at `(340, kTabY + 20, 600, rows * 44 + pad)` or content-fitted box (`w = 280..600 px`, centered horizontally, `rows = 1..9`; `shell-mpc/overlay/filter_ui.h:517-530`).
- **Content**: Scrollable vertical list of choices with checkmarks and active selection highlighting.
- **Controls inside popup**:

| Label on screen | Parameter key | Gesture | Effect | Source |
|---|---|---|---|---|
| Model Choice (F1) | `filter1.model` | Tap item / Wheel / Q turn | Selects one of 27 filter models for Filter 1 | `shell-mpc/overlay/filter_ui.h:447` |
| Style Choice (F1) | `filter1.style` | Tap item / Wheel / Q turn | Selects filter style for current model of Filter 1 | `shell-mpc/overlay/filter_ui.h:447` |
| Model Choice (F2) | `filter2.model` | Tap item / Wheel / Q turn | Selects one of 27 filter models for Filter 2 (when F2 is active) | `shell-mpc/overlay/filter_ui.h:447` |
| Style Choice (F2) | `filter2.style` | Tap item / Wheel / Q turn | Selects filter style for current model of Filter 2 (when F2 is active) | `shell-mpc/overlay/filter_ui.h:447` |
| Backdrop | - | Tap outside box | Dismisses selection list popup | `shell-mpc/overlay/filter_ui.h:84` |

### 3. Modulations Detail Page (`ModulationsPage`, `kPageMods = 22`, `shell-mpc/overlay/filter_ui.h:48, 85, 361-419, 563-636`)
- **Opened by**: Holding down any filter knob for >= 600 ms (`ChangeKind::ModsRequest` at `shell-mpc/overlay/filter_ui.h:361-374`).
- **Placement**: Full-screen page spanning the entire tab canvas `(0, kTabY, 1280, 628)`.
- **Content**: Displays all modulation matrix routings targeting the selected knob parameter. If no modulations exist yet, presents the complete modulation source board (`mods_` board, `shell-mpc/overlay/filter_ui.h:574-575`) with chips for every source (Envelopes, LFOs, Macros, Wheel, Velocity, Aftertouch, etc.). When modulations exist, shows up to 4 modulation rows with depth fader, bipolar toggle, bypass switch, and remove button, plus an "Add Source" row.
- **Controls inside detail page**:

| Label on screen | Parameter key | Gesture | Effect | Source |
|---|---|---|---|---|
| BACK | - | Tap | Dismisses Modulations page and returns to filter view | `shell-mpc/overlay/filter_ui.h:291` |
| Source Chip | - | Tap chip | Creates new modulation connection from tapped source on the full matrix board | `shell-mpc/overlay/filter_ui.h:574` |
| DEPTH | - | Drag slider / Q-Link turn | Adjusts modulation depth (-100% to +100%) for active slot | `shell-mpc/overlay/filter_ui.h:387` |
| BIPOLAR | - | Tap toggle | Toggles modulation polarity between unipolar and bipolar | `shell-mpc/overlay/filter_ui.h:389` |
| BYPASS | - | Tap toggle | Toggles modulation slot bypass / active | `shell-mpc/overlay/filter_ui.h:391` |
| REMOVE | - | Tap | Deletes modulation connection from slot | `shell-mpc/overlay/filter_ui.h:393` |
| ADD SOURCE | - | Tap stepper / Wheel | Selects modulation source to add from offered sources list | `shell-mpc/overlay/filter_ui.h:412` |
| ADD | - | Tap | Adds modulation from selected source to this parameter | `shell-mpc/overlay/filter_ui.h:292` |

## Q-Link groups

The page defines 4 primary hardware Q-Link encoder groups (`FilterFullScreen::kMaxGroups = 4`, `shell-mpc/ui/filter_display.h:763, 978-1004`, `shell-mpc/overlay/filter_ui.h:920-942`):

1. **Group 0: "MAIN"** (`shell-mpc/ui/filter_display.h:982-985`, `shell-mpc/overlay/filter_ui.h:933-940`):
   - Q1: `filterN.cutoff` (Filter cutoff frequency, 20 Hz to 20 kHz)
   - Q2: `filterN.resonance` (Filter resonance, 0% to 100%)
   - Q3: `filterN.drive` (Filter overdrive, 0 dB to 20 dB)
   - Q4: `filterN.mix` (Filter dry/wet mix, 0% to 100%)

2. **Group 1: "TRACK"** (`shell-mpc/ui/filter_display.h:987-990`, `shell-mpc/overlay/filter_ui.h:933-940`):
   - Q1: `filterN.keytrack` (Key tracking, -100% to +100%)
   - Q2: `filterN.env1` (Envelope 1 modulation toggle, 0 / 1)
   - Q3: `filterN.var` (Filter variation spread, 0% to 100%)
   - Q4: `filterN.comb_offset` (Comb blend offset, 0 to 84 semitones; Comb model only)

3. **Group 2: "OUT"** (`shell-mpc/ui/filter_display.h:992-995`, `shell-mpc/overlay/filter_ui.h:933-940`):
   - Q1: `filterN.level` (Filter output level, -60 dB to +12 dB)
   - Q2: `filterN.pan` (Filter stereo panning, -100% to +100%)
   - Q3: `filterN.blend` (Filter blend, 0% to 100%)
   - Q4: `filter.routing` (Dual filter routing: Parallel, Series 1 to 2, Series 2 to 1)

4. **Group 3: "FORMANT"** (`shell-mpc/ui/filter_display.h:997-1000`, `shell-mpc/overlay/filter_ui.h:926-928`):
   (Active only when Model is Formant, `model == 5`)
   - Q1: `filterN.formant.x` (Formant vowel X coordinate, 0% to 100%)
   - Q2: `filterN.formant.y` (Formant vowel Y coordinate, 0% to 100%)
   - Q3: `filterN.formant.transpose` (Formant vowel pitch transpose, -12 to +12 semitones)
   - Q4: `filterN.formant.spread` (Formant stereo spread, -100% to +100%)
   *(Note: `formantRes` `filterN.formant.resonance` is part of the FORMANT visual block but sits outside the 4-encoder Q group).*

**Popup and Detail Page Q-Link Groups**:
- **Output Popup ("Output", `shell-mpc/overlay/filter_ui.h:903-913`)**:
  - Q1: `filterN.destination` (Audio destination: Effects only, Direct out, None)
  - Q2: `filterN.bus1_send` (Send level to FX Bus 1, 0% to 100%)
  - Q3: `filterN.bus2_send` (Send level to FX Bus 2, 0% to 100%)
  - Q4: `filterN.level` (Output level, -60 dB to +12 dB)
- **Modulations Detail Page ("Mods", `shell-mpc/overlay/filter_ui.h:914-919`)**:
  - Maps Q-Link encoders to active modulation row amount, bipolar, and bypass parameters.

**Selection and Encoder Mapping**:
Tapping inside any parameter block or touching any of its controls automatically sets that block as the active Q-Link group (`shell-mpc/ui/filter_display.h:872-882`).
The active group maps to physical hardware encoders Q1..Q4, while the remaining groups wrap across Q5..Q16 (`shell-mpc/overlay/filter_ui.h:933-942`).

## Live parts

1. **Real-Time Frequency Response Computation and Display (`shell-mpc/overlay/curve_source.h`, `shell-mpc/overlay/filter_ui.h:105-121, 651-688`, `shell-mpc/ui/filter_display.h:213-274`)**:
   - `CurveSource` evaluates an impulse through the synth engine's real DSP filter code at the active host sample rate, rendering a 256-point frequency response curve on the log-frequency axis.
   - Dual-Filter Resultant Curve: In dual mode (`F1+F2`), `computeCombinedCurve` calculates the real-time composite frequency response according to `filter.routing`: linear magnitude summing for Parallel routing (`routing == 0`), or logarithmic dB addition for Series routing (`routing == 1` or `2`). The composite curve is rendered in bright white with a 2 px stroke.
   - Power State Dimming: When a filter's power is switched off (`filterN.on == 0`), its curve is drawn dim (alpha 0.7 stroke, 0 fill) with the label "FILTER N OFF".
2. **Interactive Drag Handles and Tooltip Bubbles (`shell-mpc/ui/filter_display.h:305-338, 383-407`)**:
   - Discs on the response curves indicate active cutoff and resonance. In dual mode, separate cyan (F1) and magenta (F2) handles are drawn simultaneously.
   - While dragging, a floating tooltip box appears adjacent to the puck, displaying formatted frequency in Hz or kHz and resonance percentage (e.g. "1.80 kHz RESO 55%"), or formant coordinates ("X 62% Y 38%").
3. **Sounding Voice Key-Track Markers (`shell-mpc/overlay/filter_ui.h:734-748`, `shell-mpc/ui/filter_display.h:275-284`)**:
   - Up to 8 faint vertical keylines are drawn across the frequency response display showing where key tracking shifts the cutoff frequency for each sounding polyphonic voice.
4. **Modulated Live Cutoff Marker (`shell-mpc/overlay/filter_ui.h:731-733`, `shell-mpc/ui/filter_display.h:285-290`)**:
   - A bright vertical marker line in `theme.flow[1]` indicates the instantaneous modulated cutoff frequency of the audible voice.
5. **Modulated Live Formant Puck (`shell-mpc/ui/filter_display.h:368-375`)**:
   - In Formant mode, if Formant X or Y is modulated, an animated concentric target disc reflects real-time voice modulation across the vowel space.
6. **Knob Modulation Depth Rings (`shell-mpc/overlay/filter_ui.h:614-622`)**:
   - Arcs and tabs around knobs update at 100 ms intervals (`kModsRefreshMs = 100.0`) to display modulation depth from the active modulation source.

## Duplicates

32 of the 47 parameter keys bound on the Filter page are duplicated on other MPC overlay tabs or internally duplicated across multiple controls:

1. **Duplicates on Mixer Page (`shell-mpc/overlay/mixer_ui.h:327-328`)**:
   - 12 parameter keys are directly duplicated as dedicated channel strip controls on the Mixer page:
     - `filter1.on`: F1 channel strip power button (`shell-mpc/overlay/mixer_ui.h:327`).
     - `filter1.level`: F1 channel strip vertical fader (`shell-mpc/overlay/mixer_ui.h:327`).
     - `filter1.pan`: F1 channel strip horizontal pan slider (`shell-mpc/overlay/mixer_ui.h:327`).
     - `filter1.destination`: F1 channel strip route dropdown button (`shell-mpc/overlay/mixer_ui.h:327`).
     - `filter1.bus1_send`: F1 channel strip Bus 1 send slider (`shell-mpc/overlay/mixer_ui.h:327`).
     - `filter1.bus2_send`: F1 channel strip Bus 2 send slider (`shell-mpc/overlay/mixer_ui.h:327`).
     - `filter2.on`: F2 channel strip power button (`shell-mpc/overlay/mixer_ui.h:328`).
     - `filter2.level`: F2 channel strip vertical fader (`shell-mpc/overlay/mixer_ui.h:328`).
     - `filter2.pan`: F2 channel strip horizontal pan slider (`shell-mpc/overlay/mixer_ui.h:328`).
     - `filter2.destination`: F2 channel strip route dropdown button (`shell-mpc/overlay/mixer_ui.h:328`).
     - `filter2.bus1_send`: F2 channel strip Bus 1 send slider (`shell-mpc/overlay/mixer_ui.h:328`).
     - `filter2.bus2_send`: F2 channel strip Bus 2 send slider (`shell-mpc/overlay/mixer_ui.h:328`).

2. **Internal Duplicates within the Filter Page**:
   - Level and Pan Knobs:
     - `filter1.level` and `filter2.level` are bound to full-screen knob full_.level (OUT block, `shell-mpc/overlay/filter_ui.h:273`) AND popup knob route_.level (`shell-mpc/overlay/filter_ui.h:287`).
     - `filter1.pan` and `filter2.pan` are bound to full-screen knob full_.pan (OUT block, `shell-mpc/overlay/filter_ui.h:274`) AND popup knob route_.pan (`shell-mpc/overlay/filter_ui.h:288`).
   - Cutoff and Resonance:
     - `filter1.cutoff` and `filter2.cutoff` are bound to rotary knob full_.cutoff (MAIN block, `shell-mpc/overlay/filter_ui.h:263`) AND interactive curve handle on full_.display (`shell-mpc/overlay/filter_ui.h:261`).
     - `filter1.resonance` and `filter2.resonance` are bound to rotary knob full_.resonance (MAIN block, `shell-mpc/overlay/filter_ui.h:264`) AND interactive curve handle on full_.display (`shell-mpc/overlay/filter_ui.h:261`).
   - Formant X and Y:
     - `filter1.formant.x` and `filter2.formant.x` are bound to rotary knob full_.formantX (FORMANT block, `shell-mpc/overlay/filter_ui.h:278`) AND interactive Formant XY-pad puck on full_.display (`shell-mpc/overlay/filter_ui.h:261`).
     - `filter1.formant.y` and `filter2.formant.y` are bound to rotary knob full_.formantY (FORMANT block, `shell-mpc/overlay/filter_ui.h:279`) AND interactive Formant XY-pad puck on full_.display (`shell-mpc/overlay/filter_ui.h:261`).
   - Destination Selection:
     - `filter1.destination` and `filter2.destination` are displayed on top-bar route button full_.route (`shell-mpc/overlay/filter_ui.h:260`) AND controlled by popup LED list route_.destination (`shell-mpc/overlay/filter_ui.h:284`).

3. **Filter 1 and Filter 2 Tab View Duplication (`shell-mpc/overlay/overlay_layout.json:47-119`)**:
   - The MPC overlay skin provides two separate tabs: "Filter 1" and "Filter 2". Both tabs instantiate the identical `FilterUi` page with the CURVES selector (`instances`, F1 | F2 | F1+F2). A user on either tab can view and control both Filter 1 and Filter 2, making the second host tab entirely redundant.

4. **Parameters Unique to the Filter Page (No duplicates on other tabs)**:
   - `filter.routing`
   - `filter1.model`, `filter1.style`, `filter1.clean_mode`, `filter1.drive`, `filter1.mix`, `filter1.keytrack`, `filter1.env1`, `filter1.var`, `filter1.comb_offset`, `filter1.blend`, `filter1.formant.transpose`, `filter1.formant.spread`, `filter1.formant.resonance`
   - `filter2.model`, `filter2.style`, `filter2.clean_mode`, `filter2.drive`, `filter2.mix`, `filter2.keytrack`, `filter2.env1`, `filter2.var`, `filter2.comb_offset`, `filter2.blend`, `filter2.formant.transpose`, `filter2.formant.spread`, `filter2.formant.resonance`

## Questionable

Judgements based on code inspection:

1. **Redundant Level and Pan Knobs in Output Route Popup (`shell-mpc/overlay/filter_ui.h:273-274, 287-288`, `shell-mpc/ui/filter_display.h:555, 651-652, 782`)**:
   - In `FilterFullScreen`, `level` and `pan` knobs are already prominent first-class controls in the OUT block (`shell-mpc/ui/filter_display.h:782`).
   - When the user taps OUTPUT to open `FilterRoutePanel`, the popup includes another set of `level` and `pan` knobs (`shell-mpc/ui/filter_display.h:555`).
   - Re-drawing LEVEL and PAN inside the popup adds zero functionality while cluttering a dialog whose sole purpose should be bus sends and destination routing.

2. **Adjacent Redundant MODEL Stepper and MODEL List MenuButton (`shell-mpc/overlay/filter_ui.h:256-257, 427`, `shell-mpc/ui/filter_display.h:770-771, 909-910`)**:
   - The `model` Stepper supports left/right chevron stepping, and tapping its center opens the 27-model `ListPopup` (`shell-mpc/overlay/filter_ui.h:427`).
   - Directly next to it sits `modelList`, a dedicated 56 x 56 px button with a downward chevron that performs the identical action (`shell-mpc/overlay/filter_ui.h:427`).
   - Consuming 56 px of top bar width for a redundant trigger button is wasteful, especially when the `style` Stepper right next to it opens its own list popup on center tap without requiring a secondary button.

3. **Redundant Dual Host Tabs vs In-Page CURVES Switcher (`shell-mpc/overlay/overlay_layout.json:47-119`, `shell-mpc/overlay/filter_ui.h:95-97, 436-444`)**:
   - Having two independent tabs ("Filter 1" and "Filter 2") in the host skin is redundant when `FilterUi` already contains the CURVES selector (`instances`, F1 | F2 | F1+F2) in the top bar.
   - Users can switch between Filter 1 and Filter 2 from either tab. Retaining two tabs wastes one of the precious top-level MPC navigation slots.

4. **Comb Blend Offset Knob Inactive on 26 of 27 Filter Models (`shell-mpc/overlay/filter_ui.h:878`, `shell-mpc/ui/filter_display.h:779, 950`)**:
   - `combOffset` ("COMB") permanently occupies the fourth slot of the TRACK block and Q-Link encoder Q4.
   - However, `FilterUi::pullControls` explicitly disables `combOffset` for all models except Model 6 Comb (`shell-mpc/overlay/filter_ui.h:878`).
   - Across 26 filter models (Analog, Dirty, Ladder, Digital, Diode, Formant, Phaser, Multi SVF, etc.), this knob remains dimmed and inert, leaving encoder Q4 completely non-functional in the TRACK group. A model-dependent context parameter (or hiding it) would be far more intuitive.

5. **Formant Resonance Knob Excluded from Hardware Q-Link Encoders (`shell-mpc/ui/filter_display.h:967, 997-1000`, `shell-mpc/overlay/filter_ui.h:926-928`)**:
   - When switching to Formant mode, the 5-knob FORMANT block is revealed.
   - Group 3 ("FORMANT") assigns Q1..Q4 to `formantX`, `formantY`, `formantTrans`, and `formantSpread` (`shell-mpc/ui/filter_display.h:997-1000`).
   - `formantRes` ("RESONANCE") is placed in a separate full-height column and is entirely omitted from the Q-Link encoder group (`shell-mpc/ui/filter_display.h:967`). A user using hardware encoders cannot adjust Formant peak resonance without reaching for the touchscreen.

6. **Hidden CLOSE Button Maintained in Page Mode Layout Logic (`shell-mpc/ui/filter_display.h:767, 827-833, 903`)**:
   - `FilterFullScreen` instantiates a `close` button that is unconditionally hidden in page mode (`setPageMode(true)` at `shell-mpc/overlay/filter_ui.h:69`).
   - Layout code in `layout()` still branches on `pageMode_ ? 0 : 84` (`shell-mpc/ui/filter_display.h:903`). This is dead visual code retained from older floating modal designs.

7. **Extreme Redundancy between Filter Page Output Popup and Mixer Page (`shell-mpc/overlay/mixer_ui.h:327-328` vs `shell-mpc/overlay/filter_ui.h:284-288`)**:
   - All 6 parameters of the `FilterRoutePanel` popup (`filterN.destination`, `filterN.bus1_send`, `filterN.bus2_send`, `filterN.level`, `filterN.pan`, plus `filterN.on`) are mirrored identically on the dedicated Mixer page (`shell-mpc/overlay/mixer_ui.h:327-328`).
   - Having two disparate places to adjust filter levels and bus routing creates confusion over where mixing decisions should be made.
