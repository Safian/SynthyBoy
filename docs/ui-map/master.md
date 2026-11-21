# Master UI Map

## Purpose
The Master page is the full-screen MPC overlay view for the "Master" tab (`shell-mpc/overlay/overlay_layout.json:602-615`, `scripts/overlay-skin/mpc-ui-pages-overlay.json:155-161`, `docs/MPC.md:1342-1358`).
It is implemented by `MasterUi` (`shell-mpc/overlay/master_ui.h:34-528`) using `MasterScreen` (`shell-mpc/ui/voice_master.h:426-794`).
The view occupies the full tab area (1280 x 628) and provides comprehensive top-level synthesis controls organized into two switchable sides:
1. **MAIN side**:
   - **OUTPUT**: Master output volume (`master.volume`), stereo pan spread (`master.stereo_spread`), stereo routing percentage (`master.stereo_routing`), voice override behavior (`master.voice_override`), L/R peak output meters with dB scale and peak hold, output overload clip indicator, plugin lookahead output limiter, and engine rendering quality toggles (`quality.no_smoothing`, `quality.oversample_4x`).
   - **PITCH**: Master fine tuning (`master.tune`), master transposition (`master.transpose`), pitch bend wheel up range (`master.bend_range`), pitch bend wheel down range (`master.bend_down_range`), and bend range link switch (`master.bend_down_link`).
   - **VOICES**: Maximum synth polyphony (`master.polyphony`), monophonic legato (`master.legato`), polyphonic legato (`master.poly_legato`), oscillator phase legato (`voice.phase_legato`), velocity tracking sensitivity (`master.velocity_track`), same note retrigger behavior (`master.voice.same_note`), voice stealing priority rule (`master.voice.steal_priority`), sounding voice count meter, and plugin pad pressure modulation layer.
   - **GLIDE**: Portamento glide duration (`master.portamento.time`), glide curve slope (`master.portamento.slope`), continuous glide mode (`master.portamento.always`), interval-scaled glide time (`master.portamento.scaled`), and live portamento curvature display.
2. **TUNING & MPE side**:
   - **SCALE**: Scale selection (`master.scale.type` with 20 musical scales), root key selection (`master.scale.key` with 12 chromatic pitches), scale quantization mode (`master.scale.mode`), and interactive 12-key piano keyboard diagram.
   - **TUNING**: A4 reference pitch tuning (`master.tuning.a4`), microtonal tuning temperament system (`master.tuning.scale`), MIDI Tuning Standard ESP client support (`master.tuning.mts_esp`), and MTS Note-On filter (`master.tuning.mts_note_on_only`).
   - **MPE**: MIDI Polyphonic Expression toggle (`master.mpe.on`), per-note MPE pitch bend range (`master.mpe.bend_range`), MPE XYZ to macros routing (`master.mpe.xyz_macro`), MPE Y to mod wheel mapping (`master.mpe.y_modwheel`), standard pitch bend to MPE X glide mapping (`master.mpe.bend_to_x`), and MPE Y bipolar mode (`master.mpe.y_bipolar`).

## Layout
The Master page occupies the entire MPC overlay tab region `(0, 110, 1280, 628)` (`shell-mpc/overlay/overlay_layout.json:606-611`, `shell-mpc/overlay/master_ui.h:72, 83-84`, `shell-mpc/ui/voice_master.h:535`).
The page structure consists of a top header bar and two switchable side views containing distinct functional blocks:

1. **Header Area** (y = 114 to y = 170, height 56 px, header lane 64 px):
   - Page Title: "MASTER" at `(rect.x + 14, rect.y + 4, 270, 56)` (`shell-mpc/ui/voice_master.h:764`).
   - Side Switcher Buttons:
     - `sideMain`: "MAIN" at `(rect.x + 300, rect.y + 4, 160, 56)` (`shell-mpc/ui/voice_master.h:661`).
     - `sideTuning`: "TUNING & MPE" at `(rect.x + 468, rect.y + 4, 220, 56)` (`shell-mpc/ui/voice_master.h:662`).
     - Active side is highlighted with a 1.6 px outline in flow accent color (`shell-mpc/ui/voice_master.h:765`).
   - Horizontal separator line below header at y = 172 (`shell-mpc/ui/voice_master.h:755`).

2. **Side 0: MAIN** (`shell-mpc/ui/voice_master.h:664-719`):
   Divided into a 2x2 asymmetric block grid:
   - **Row 1 (Top, y = 182, height = 228 px)**:
     - **Block 0: OUTPUT** at `(rect.x + 8, rect.y + 72, 640, 228)` (`shell-mpc/ui/voice_master.h:631, 667-683`):
       - Clip indicator `clip` (`ClipLabel`, width 100, height 56) at `(in.x, in.y)`.
       - Dual Peak Meter `meters` (`PeakMeterDisplay`, width 100, height 128) below clip label with dB scale ticks (0, -6, -12, -24, -48 dB) and L/R peak bars.
       - Three large rotary knobs across upper area: `volume` ("VOLUME"), `spread` ("SPREAD"), and `stereoRouting` ("STEREO"), each width 114 px, height 152 px.
       - LED list `voiceOverride` ("VOICE OVERRIDE": Kill, Steal) at `(in.right() - 150, in.y, 150, 128)`.
       - Bottom strip (height 28 px): LED toggle `outputLimiter` ("LIMIT", width >= 96 px), label "QUALITY" (width 70 px), LED toggle `noSmoothing` ("NO SMOOTHING"), and LED toggle `oversample4x` ("OVERSAMPLE 4X").
     - **Block 1: PITCH** at `(rect.x + 656, rect.y + 72, 616, 228)` (`shell-mpc/ui/voice_master.h:633, 685-692`):
       - Top row: medium knob `tune` ("TUNE", width 190 px, side-dial layout) followed by three NumberFields: `transpose` ("TRANSPOSE", width 132 px), `bendUp` ("BEND UP", width 132 px), and `bendDown` ("BEND DOWN", width 126 px).
       - Lower strip: LED toggle `bendLink` ("LINK") positioned directly beneath `bendUp`.
   - **Row 2 (Bottom, y = 418, height = 312 px)**:
     - **Block 2: VOICES** at `(rect.x + 8, rect.y + 308, 780, 312)` (`shell-mpc/ui/voice_master.h:631, 695-710`):
       - Row 1: Four cells (width 186 px, height 28 px): NumberField `polyphony` ("POLY") and three LED toggles `legato` ("LEGATO"), `polyLegato` ("POLY LEG"), and `phaseLegato` ("PHASE LEG").
       - Row 2: NumberField `velocityTrack` ("VEL TRACK", width 186 px, height 56 px), LED list `sameNote` ("SAME NOTE", width 221 px, height 56 px, 3 options: Steal, Stack, Ignore), and LED list `stealPriority` ("STEAL", width 345 px, height 56 px, 5 options: Newest, Oldest, Highest, Lowest, Velocity).
       - Row 3: LED toggle `padPressure` ("PAD PRESS", width 186 px, height 28 px), NumberField `padAmount` ("PAD %", width 186 px, height 28 px), and live voice count bar `voiceMeter` (`VoiceMeterDisplay`, width 380 px, height 44 px).
     - **Block 3: GLIDE** at `(rect.x + 796, rect.y + 308, 476, 312)` (`shell-mpc/ui/voice_master.h:633, 712-719`):
       - Left section (width 216 px): two medium knobs `portaTime` ("GLIDE", width 104 px) and `portaSlope` ("SLOPE", width 104 px) on top; two LED toggles `portaAlways` ("ALWAYS", width 104 px) and `portaScaled` ("SCALED", width 104 px) below.
       - Right section (width 240 px, height 268 px): Portamento curve display `portaCurve` (`PortamentoCurveDisplay`) showing exponential glide shape.

3. **Side 1: TUNING & MPE** (`shell-mpc/ui/voice_master.h:721-750`):
   Divided into three blocks:
   - **Row 1 (Top, y = 182, height = 280 px)**:
     - **Block 4: SCALE** at `(rect.x + 8, rect.y + 72, 1264, 280)` (`shell-mpc/ui/voice_master.h:637, 722-729`):
       - Top row (height 64 px): Stepper `scaleType` ("SCALE", width 450 px), Stepper `scaleKey` ("KEY", width 325 px), and LED list `scaleMode` ("MODE", width 461 px, 2 options: Conform, Mark only).
       - Lower area (height 162 px): `keyboard` (`MiniKeyboardDisplay`, width 1252 px) featuring interactive 12-key piano keyboard (7 white keys C..B, 5 black keys) with active scale note highlights.
   - **Row 2 (Bottom, y = 470, height = 260 px)**:
     - **Block 5: TUNING** at `(rect.x + 8, rect.y + 360, 628, 260)` (`shell-mpc/ui/voice_master.h:639, 731-738`):
       - Top row (height 169 px): NumberField `tuningA4` ("A4 HZ", width 200 px) and LED list `tuningScale` ("TUNING", width 408 px, 4 options: 12-TET, Pythagorean, Just intonation, User file).
       - Bottom row (height 28 px): LED toggle `mtsEsp` ("MTS-ESP", width 304 px) and LED toggle `mtsNoteOn` ("NOTE-ON", width 304 px).
     - **Block 6: MPE** at `(rect.x + 644, rect.y + 360, 628, 260)` (`shell-mpc/ui/voice_master.h:639, 740-749`):
       - Top row (height 169 px): LED toggle `mpeOn` ("MPE", width 200 px), NumberField `mpeBend` ("MPE BEND", width 200 px), and LED list `mpeMacro` ("XYZ TO MACROS", width 200 px, options: Off, XYZ, YZ).
       - Bottom row (height 28 px): LED toggle `mpeYModwheel` ("Y TO WHEEL", width 200 px), LED toggle `mpeBendToX` ("BEND TO X", width 200 px), and LED toggle `mpeYBipolar` ("Y BIPOLAR", width 200 px).

## Controls
| Label on screen | Parameter key | Gesture | Effect | Source |
|---|---|---|---|---|
| MAIN | - | Tap | Switches view to the Main side (OUTPUT, PITCH, VOICES, GLIDE blocks) | `shell-mpc/ui/voice_master.h:609` |
| TUNING & MPE | - | Tap | Switches view to the Tuning & MPE side (SCALE, TUNING, MPE blocks) | `shell-mpc/ui/voice_master.h:610` |
| CLIP | - | Tap | Clears sticky output clip overload warning flag | `shell-mpc/overlay/master_ui.h:85` |
| VOLUME | `master.volume` | Rotary drag / Wheel / Q1 turn / Tap value | Sets master output volume (-60.0 to +6.0 dB, tap value opens text keyboard) | `shell-mpc/overlay/master_ui.h:240` |
| SPREAD | `master.stereo_spread` | Rotary drag / Wheel / Q2 turn / Tap value | Sets stereo pan spread across voices (0% to 100%, tap value opens text keyboard) | `shell-mpc/overlay/master_ui.h:241` |
| STEREO | `master.stereo_routing` | Rotary drag / Wheel / Q3 turn / Tap value | Sets stereo routing crossmix percentage (0% to 100%, tap value opens text keyboard) | `shell-mpc/overlay/master_ui.h:242` |
| VOICE OVERRIDE | `master.voice_override` | Tap option / Wheel / Q4 turn | Sets voice allocation override mode (Kill, Steal) | `shell-mpc/overlay/master_ui.h:243` |
| LIMIT | - | Tap toggle | Toggles plugin output lookahead peak limiter on / off (shell setting) | `shell-mpc/overlay/master_ui.h:274` |
| NO SMOOTHING | `quality.no_smoothing` | Tap toggle | Disables parameter interpolation smoothing across blocks (Off / On) | `shell-mpc/overlay/master_ui.h:275` |
| OVERSAMPLE 4X | `quality.oversample_4x` | Tap toggle | Enables 4x internal oversampling for synthesis engine (Off / On) | `shell-mpc/overlay/master_ui.h:276` |
| TUNE | `master.tune` | Rotary drag / Wheel / Q1 turn / Tap value | Sets master fine tuning (-100 to +100 cents, tap value opens text keyboard) | `shell-mpc/overlay/master_ui.h:244` |
| TRANSPOSE | `master.transpose` | Vertical drag / Wheel / Q2 turn | Sets master pitch transposition (-48 to +48 semitones) | `shell-mpc/overlay/master_ui.h:245` |
| BEND UP | `master.bend_range` | Vertical drag / Wheel / Q3 turn | Sets pitch bend wheel up range (0 to 24 semitones) | `shell-mpc/overlay/master_ui.h:246` |
| BEND DOWN | `master.bend_down_range` | Vertical drag / Wheel / Q4 turn | Sets pitch bend wheel down range (0 to 24 semitones) | `shell-mpc/overlay/master_ui.h:247` |
| LINK | `master.bend_down_link` | Tap toggle / Q1 turn | Links pitch bend down range to pitch bend up range (Off / On) | `shell-mpc/overlay/master_ui.h:248` |
| POLY | `master.polyphony` | Vertical drag / Wheel / Q1 turn | Sets maximum synth polyphony (1 to 32 voices) | `shell-mpc/overlay/master_ui.h:249` |
| LEGATO | `master.legato` | Tap toggle / Q2 turn | Enables monophonic legato envelope retriggering (Off / On) | `shell-mpc/overlay/master_ui.h:250` |
| POLY LEG | `master.poly_legato` | Tap toggle / Q3 turn | Enables polyphonic legato envelope retriggering (Off / On) | `shell-mpc/overlay/master_ui.h:251` |
| PHASE LEG | `voice.phase_legato` | Tap toggle / Q4 turn | Preserves oscillator phase on legato note transitions (Off / On) | `shell-mpc/overlay/master_ui.h:252` |
| VEL TRACK | `master.velocity_track` | Vertical drag / Wheel / Q1 turn | Sets velocity tracking sensitivity (-100% to +100%) | `shell-mpc/overlay/master_ui.h:255` |
| SAME NOTE | `master.voice.same_note` | Tap option / Wheel / Q2 turn | Sets voice behavior when same note is retriggered (Steal, Stack, Ignore) | `shell-mpc/overlay/master_ui.h:253` |
| STEAL | `master.voice.steal_priority` | Tap option / Wheel / Q3 turn | Sets voice stealing priority rule (Newest, Oldest, Highest, Lowest, Velocity) | `shell-mpc/overlay/master_ui.h:254` |
| PAD PRESS | - | Tap toggle | Enables / disables plugin pad pressure routing layer (shell setting) | `shell-mpc/overlay/master_ui.h:277` |
| PAD % | - | Vertical drag / Wheel | Sets pad pressure modulation amount (0% to 100%, shell setting) | `shell-mpc/overlay/master_ui.h:278` |
| GLIDE | `master.portamento.time` | Rotary drag / Wheel / Q1 turn / Tap value | Sets portamento glide duration (0 to 8000 ms, tap value opens text keyboard) | `shell-mpc/overlay/master_ui.h:256` |
| SLOPE | `master.portamento.slope` | Rotary drag / Wheel / Q2 turn / Tap value | Sets portamento glide slope curvature (-8.0 concave to +8.0 convex, tap value opens text keyboard) | `shell-mpc/overlay/master_ui.h:257` |
| ALWAYS | `master.portamento.always` | Tap toggle / Q3 turn | Enables portamento glide on all notes, not just legato overlaps (Off / On) | `shell-mpc/overlay/master_ui.h:258` |
| SCALED | `master.portamento.scaled` | Tap toggle / Q4 turn | Scales portamento glide time by pitch interval distance (Off / On) | `shell-mpc/overlay/master_ui.h:259` |
| SCALE | `master.scale.type` | Tap arrows / Turn / Q1 turn / Tap caption | Selects active musical scale (20 options; tap caption opens list popup) | `shell-mpc/overlay/master_ui.h:260` |
| KEY | `master.scale.key` | Tap arrows / Turn / Q2 turn / Tap caption | Selects musical root key (12 options C..B; tap caption opens list popup) | `shell-mpc/overlay/master_ui.h:261` |
| MODE | `master.scale.mode` | Tap option / Wheel / Q3 turn | Sets scale quantization mode (Conform, Mark only) | `shell-mpc/overlay/master_ui.h:263` |
| Scale Keyboard | `master.scale.key` | Tap or drag piano key | Selects musical root key (0..11, C..B) by touching piano keys on mini keyboard | `shell-mpc/overlay/master_ui.h:262` |
| A4 HZ | `master.tuning.a4` | Vertical drag / Wheel / Q1 turn | Sets reference concert pitch frequency for A4 (400.00 to 480.00 Hz in 0.01 Hz steps) | `shell-mpc/overlay/master_ui.h:264` |
| TUNING | `master.tuning.scale` | Tap option / Wheel / Q2 turn | Selects microtonal tuning temperament system (12-TET, Pythagorean, Just intonation, User file) | `shell-mpc/overlay/master_ui.h:265` |
| MTS-ESP | `master.tuning.mts_esp` | Tap toggle / Q3 turn | Enables MIDI Tuning Standard ESP client synchronization (Off / On) | `shell-mpc/overlay/master_ui.h:266` |
| NOTE-ON | `master.tuning.mts_note_on_only` | Tap toggle / Q4 turn | Restricts MTS tuning updates to Note-On events only (Off / On) | `shell-mpc/overlay/master_ui.h:267` |
| MPE | `master.mpe.on` | Tap toggle / Q1 turn | Enables MIDI Polyphonic Expression MPE mode (Off / On) | `shell-mpc/overlay/master_ui.h:268` |
| MPE BEND | `master.mpe.bend_range` | Vertical drag / Wheel / Q2 turn | Sets per-note MPE pitch bend range (1 to 96 semitones) | `shell-mpc/overlay/master_ui.h:269` |
| XYZ TO MACROS | `master.mpe.xyz_macro` | Tap option / Wheel / Q3 turn | Routes MPE X/Y/Z expressive dimensions to Macros (Off, XYZ, YZ) | `shell-mpc/overlay/master_ui.h:270` |
| Y TO WHEEL | `master.mpe.y_modwheel` | Tap toggle / Q1 turn | Maps MPE Y dimension (CC74 timbre) to standard mod wheel CC1 (Off / On) | `shell-mpc/overlay/master_ui.h:271` |
| BEND TO X | `master.mpe.bend_to_x` | Tap toggle / Q2 turn | Maps standard pitch bend to MPE X horizontal glide dimension (Off / On) | `shell-mpc/overlay/master_ui.h:272` |
| Y BIPOLAR | `master.mpe.y_bipolar` | Tap toggle / Q3 turn | Treats MPE Y dimension as bipolar centered at 64 (-1.0 to +1.0) instead of unipolar (Off / On) | `shell-mpc/overlay/master_ui.h:273` |

## Opens
The Master page can open two detail/popup overlays via the UI stack (`UiStack`, `shell-mpc/overlay/master_ui.h:438, 455`):

1. **List Selection Popup (`ui::ListPopup list_`, `kPageList = 15`)**:
   - **Trigger**: Tapping the caption or center zone of either stepper: `scaleType` ("SCALE") or `scaleKey` ("KEY") (`shell-mpc/overlay/master_ui.h:428-442`). Both steppers are configured with `listable = true` (`shell-mpc/overlay/master_ui.h:279-280`), causing center taps to emit `ChangeKind::Request`.
   - **Bounds**: Centered over the tab at `(340, 130, 600, 460)` (`shell-mpc/overlay/master_ui.h:87`).
   - **Controls inside popup**:
     | Label on screen | Parameter key | Gesture | Effect | Source |
     |---|---|---|---|---|
     | Choice Item | `master.scale.type` or `master.scale.key` | Tap row / Vertical scroll drag | Selects option, writes plain index to active stepper parameter, and closes popup | `shell-mpc/overlay/master_ui.h:369` |
     | Backdrop | - | Tap outside popup box | Dismisses list popup without changing parameter (`kListBackdrop = 701`) | `shell-mpc/overlay/master_ui.h:364` |

2. **Virtual Numeric Keyboard (`ui::TextEntry entry_`, `kPageKeyboard = 16`)**:
   - **Trigger**: Tapping the numeric value readout of any Knob on the page (`volume`, `spread`, `stereoRouting`, `tune`, `portaTime`, `portaSlope`). Tapping the value readout emits `ChangeKind::Request` (`shell-mpc/ui/base.h:398-401`), which opens the text entry page (`shell-mpc/overlay/master_ui.h:443-458`).
   - **Bounds**: Full tab area `(0, 110, 1280, 628)` (`shell-mpc/overlay/master_ui.h:86`).
   - **Controls inside keyboard**:
     | Label on screen | Parameter key | Gesture | Effect | Source |
     |---|---|---|---|---|
     | Keypad Buttons | - | Tap key (0..9, `-`, `.`) | Appends digit or punctuation symbol to numeric input buffer | `shell-mpc/ui/text_entry.h:122` |
     | Backspace | - | Tap key | Deletes previous character in input buffer | `shell-mpc/ui/text_entry.h:125` |
     | Layer Switch | - | Tap key | Toggles between letters and symbols keypad layers | `shell-mpc/ui/text_entry.h:139` |
     | CANCEL | - | Tap button | Closes virtual keyboard without applying changes (`kKeyboardCancel = 711`) | `shell-mpc/overlay/master_ui.h:376` |
     | OK | - | Tap button | Parses entered text via `strtod`, clamps to parameter range, rounds to 1 decimal place, writes value to the target knob parameter, and closes keyboard (`kKeyboardOk = 710`) | `shell-mpc/overlay/master_ui.h:381` |

3. **Modulation Pages**:
   - Not opened. Even though `Knob::tick` and `NumberField::tick` emit `ChangeKind::ModsRequest` on a 600 ms long press (`shell-mpc/ui/base.h:421, 612`), `MasterUi::handle` (`shell-mpc/overlay/master_ui.h:360-473`) does not implement a handler for `ChangeKind::ModsRequest`. Long presses on knobs or number fields have no effect.

## Q-Link groups
The page defines 10 Q-Link groups across its two sides (`MasterScreen::kMaxGroups = 10`, `shell-mpc/ui/voice_master.h:429, 780-793`, `shell-mpc/overlay/master_ui.h:476-504`):

### Side Main (Groups 0 to 5)
1. **Group 0: "OUTPUT"** (`shell-mpc/ui/voice_master.h:782`, `shell-mpc/overlay/master_ui.h:480`):
   - Q1: `master.volume`
   - Q2: `master.stereo_spread`
   - Q3: `master.stereo_routing`
   - Q4: `master.voice_override`

2. **Group 1: "PITCH"** (`shell-mpc/ui/voice_master.h:783`, `shell-mpc/overlay/master_ui.h:481`):
   - Q1: `master.tune`
   - Q2: `master.transpose`
   - Q3: `master.bend_range`
   - Q4: `master.bend_down_range`

3. **Group 2: "LINK"** (`shell-mpc/ui/voice_master.h:784`, `shell-mpc/overlay/master_ui.h:482`):
   - Q1: `master.bend_down_link`
   - Q2..Q4: unassigned

4. **Group 3: "VOICES"** (`shell-mpc/ui/voice_master.h:785`, `shell-mpc/overlay/master_ui.h:483`):
   - Q1: `master.polyphony`
   - Q2: `master.legato`
   - Q3: `master.poly_legato`
   - Q4: `voice.phase_legato`

5. **Group 4: "VOICE OPTS"** (`shell-mpc/ui/voice_master.h:786`, `shell-mpc/overlay/master_ui.h:484`):
   - Q1: `master.velocity_track`
   - Q2: `master.voice.same_note`
   - Q3: `master.voice.steal_priority`
   - Q4: unassigned

6. **Group 5: "GLIDE"** (`shell-mpc/ui/voice_master.h:787`, `shell-mpc/overlay/master_ui.h:485`):
   - Q1: `master.portamento.time`
   - Q2: `master.portamento.slope`
   - Q3: `master.portamento.always`
   - Q4: `master.portamento.scaled`

### Side Tuning & MPE (Groups 6 to 9)
7. **Group 6: "SCALE"** (`shell-mpc/ui/voice_master.h:788`, `shell-mpc/overlay/master_ui.h:486`):
   - Q1: `master.scale.type`
   - Q2: `master.scale.key`
   - Q3: `master.scale.mode`
   - Q4: unassigned

8. **Group 7: "TUNING"** (`shell-mpc/ui/voice_master.h:789`, `shell-mpc/overlay/master_ui.h:487`):
   - Q1: `master.tuning.a4`
   - Q2: `master.tuning.scale`
   - Q3: `master.tuning.mts_esp`
   - Q4: `master.tuning.mts_note_on_only`

9. **Group 8: "MPE"** (`shell-mpc/ui/voice_master.h:790`, `shell-mpc/overlay/master_ui.h:488`):
   - Q1: `master.mpe.on`
   - Q2: `master.mpe.bend_range`
   - Q3: `master.mpe.xyz_macro`
   - Q4: unassigned

10. **Group 9: "MPE Y"** (`shell-mpc/ui/voice_master.h:791`, `shell-mpc/overlay/master_ui.h:489`):
    - Q1: `master.mpe.y_modwheel`
    - Q2: `master.mpe.bend_to_x`
    - Q3: `master.mpe.y_bipolar`
    - Q4: unassigned

### Q-Link Interaction and Hardware Routing
- Group activation: Touching any control inside a block activates its corresponding Q-Link group (`shell-mpc/ui/voice_master.h:574-604`).
- Encoder routing: On the active side, the selected group is mapped to hardware encoders Q1..Q4; the subsequent 3 groups wrap within that side to fill encoders Q5..Q16 (`first = 0, count = 6` on Main; `first = 6, count = 4` on Tuning) (`shell-mpc/overlay/master_ui.h:491-501`).
- When a detail popup (`list_` or `entry_`) is active, all Q-Link hardware encoder targets are cleared (`shell-mpc/overlay/master_ui.h:494-495`).
- Visual feedback: The active group displays an accent frame in color `T.q` (`QGroup::draw`), while inactive groups display subtle idle chips (`QGroup::drawIdle`) (`shell-mpc/ui/voice_master.h:617-622`).
- Touch-only controls: `LIMIT` (`outputLimiter`), `QUALITY` (`noSmoothing`, `oversample4x`), `PAD PRESS` (`padPressure`), `PAD %` (`padAmount`), and `Scale Keyboard` (`keyboard`) have no Q-Link encoder mappings and are adjusted exclusively via touchscreen.

## Live parts
1. **Master Peak Meters (`meters`, `PeakMeterDisplay`)**:
   - In `MasterUi::update` (`shell-mpc/overlay/master_ui.h:101-104`), master output peak audio level is read from engine live values `in.live->stripPeak[9]` (representing the louder of L and R master channels).
   - In `PeakMeterDisplay::drawBody` (`shell-mpc/ui/voice_master.h:303-369`), both L and R channels render dynamic vertical level bars using the color gradient ramp (cyan to magenta, turning red upon clip `> 1.0`).
   - A floating peak hold line rises with instantaneous peaks and decays downward at 0.5 (30 dB) per second (`PeakMeterDisplay::updateHold`, `shell-mpc/ui/voice_master.h:276-281`).
   - A dedicated graduation scale column on the left renders dB tick marks and numbers at 0, -6, -12, -24, and -48 dB (`shell-mpc/ui/voice_master.h:320-330`).

2. **Clip Overload Warning Indicator (`clip`, `ClipLabel`)**:
   - In `MasterUi::update` (`shell-mpc/overlay/master_ui.h:98`), `inst.takeClipFlag()` is queried. If the engine limiter or safety ceiling detects audio magnitude exceeding 1.8, the red "CLIP" label lights up and holds for 2.0 seconds (`shell-mpc/ui/clip_label.h:20-56`). A tap on the label resets the hold indicator immediately (`shell-mpc/overlay/master_ui.h:408-412`).

3. **Active Voice Count Bar (`voiceMeter`, `VoiceMeterDisplay`)**:
   - In `MasterUi::update` (`shell-mpc/overlay/master_ui.h:99-100`), `in.live->activeVoices` and `inst.param(polyphonyIdx_)` are polled.
   - In `VoiceMeterDisplay::drawBody` (`shell-mpc/ui/voice_master.h:384-419`), a segmented bar containing up to 32 individual cells is drawn. Sounding voices (`0 .. activeVoices - 1`) are illuminated in signal flow accent color `T.flow[0]`, while unused voice slots remain dark. Dynamic text displays `"<active> / <polyphony> VOICES"`.

4. **Portamento Glide Curve Diagram (`portaCurve`, `PortamentoCurveDisplay`)**:
   - In `MasterUi::update` (`shell-mpc/overlay/master_ui.h:123`), `master.portamento.slope` is read from `inst.param(slopeIdx_)`.
   - In `PortamentoCurveDisplay::drawBody` (`shell-mpc/ui/voice_master.h:35-72`), a 32-segment exponential curve $y = (e^{k \cdot t} - 1) / (e^k - 1)$ is dynamically calculated and drawn in real time across the grid, visualizing linear (slope = 0), concave (slope < 0), or convex (slope > 0) glide acceleration.

5. **Scale Mini Keyboard Diagram (`keyboard`, `MiniKeyboardDisplay`)**:
   - In `MasterUi::update` (`shell-mpc/overlay/master_ui.h:124-126`), `master.scale.type`, `master.scale.key`, and `master.scale.mode` are refreshed.
   - In `MiniKeyboardDisplay::drawBody` (`shell-mpc/ui/voice_master.h:202-246`), all 12 chromatic keys are rendered. In-scale notes are highlighted with light backgrounds and glowing outlines, while the root pitch receives a solid flow accent indicator bar. The header caption dynamically reflects the active scale root: `"ALL 12 NOTES"` for chromatic or `"SCALE ON <KEY>"` for chosen scales.

6. **Active Q-Link Group Outline and Idle Chips**:
   - The active Q-Link group draws a bright accent frame around its 4 controls (`QGroup::draw`, `shell-mpc/ui/voice_master.h:617-618`).
   - Inactive groups on the visible side draw quiet idle chips with slot numbers (`QGroup::drawIdle`, `shell-mpc/ui/voice_master.h:619-622`).

## Duplicates
- `master.volume`: Also appears on the Mixer overlay page (`MixerUi`, `shell-mpc/overlay/mixer_ui.h:331`) as the primary level fader of strip 9 ("MASTER").
- Note on `voice.phase_legato`: Carries the `voice.` prefix but is bound exclusively on the Master page (`shell-mpc/overlay/master_ui.h:252`) and is absent from the Voice page (`shell-mpc/overlay/voice_ui.h`).
- None of the other 34 parameter keys bound on this page (`master.stereo_spread`, `master.stereo_routing`, `master.voice_override`, `master.tune`, `master.transpose`, `master.bend_range`, `master.bend_down_range`, `master.bend_down_link`, `master.polyphony`, `master.legato`, `master.poly_legato`, `master.voice.same_note`, `master.voice.steal_priority`, `master.velocity_track`, `master.portamento.time`, `master.portamento.slope`, `master.portamento.always`, `master.portamento.scaled`, `master.scale.type`, `master.scale.key`, `master.scale.mode`, `master.tuning.a4`, `master.tuning.scale`, `master.tuning.mts_esp`, `master.tuning.mts_note_on_only`, `master.mpe.on`, `master.mpe.bend_range`, `master.mpe.xyz_macro`, `master.mpe.y_modwheel`, `master.mpe.bend_to_x`, `master.mpe.y_bipolar`, `quality.no_smoothing`, `quality.oversample_4x`) appear on any other overlay page or tab under `shell-mpc/`.

## Questionable
1. **NumberField controls write step index instead of parameter value (Bug)**:
   - In `shell-mpc/overlay/master_ui.h:467-469`, `handle()` processes `ChangeKind::Index` with:
     ```cpp
     case ui::ChangeKind::Index:
         uibind::writePlain (inst, it->param, ch.index);
         break;
     ```
   - When a user touches or drags any `NumberField` (`transpose`, `bendUp`, `bendDown`, `polyphony`, `velocityTrack`, `tuningA4`, `mpeBend`), `NumberField` emits `Change::indexed (id_, idx, state.min + idx * stepSize())` (`shell-mpc/ui/base.h:621`).
   - The member ch.index contains the 0-based step index (`idx`), while ch.value contains the actual calculated value (`state.min + idx * stepSize()`).
   - Writing ch.index corrupts parameter values whenever fields are dragged or turned:
     - `master.transpose` (range -48 to +48): index 48 (center/0 st) writes 48 semitones to the engine instead of 0.
     - `master.tuning.a4` (range 400.00 to 480.00 Hz, step 0.01): index 0..8000 writes 0..8000 Hz instead of 400..480 Hz.
     - `master.velocity_track` (range -100 to +100%): index 100 (0%) writes 100% sensitivity.
     - `master.polyphony` (range 1 to 32): 1 voice has index 0, writing 0 voices to the engine instead of 1.
   - Note that `padAmount` in `shell-mpc/overlay/master_ui.h:421-422` specifically used ch.value to avoid this bug (`inst.setPadPressure (on, static_cast<float> (ch.value));`), but the general item handler failed to do so.

2. **Knob virtual keyboard dialog title hardcodes "dB" unit for all parameters (Cosmetic/Confusing)**:
   - In `shell-mpc/overlay/master_ui.h:453`:
     ```cpp
     std::snprintf (title, sizeof title, "%s (%.1f .. %.1f dB)", pi.name, pi.min, pi.max);
     ```
   - Tapping the numeric readout of any Knob opens `entry_` to allow typing a value. However, the title string unconditionally appends `"dB"` regardless of the parameter's actual unit.
   - For example, tapping `TUNE` displays `"Master Tune (-100.0 .. 100.0 dB)"` (actual unit: cents), and tapping `GLIDE` displays `"Portamento Time (0.0 .. 8000.0 dB)"` (actual unit: ms).

3. **Unhandled `ChangeKind::ModsRequest` (Unwired modulation details)**:
   - `Knob::tick` (`shell-mpc/ui/base.h:421`) and `NumberField::tick` (`shell-mpc/ui/base.h:612`) emit `ChangeKind::ModsRequest` after a 600 ms long press.
   - `docs/MPC.md:1330` states: *"All elements bind to engine parameters with ui_bind.h ... and open the modulations of a control (long press) and the sources' target list with the shared ModulationsPage / ModTargetsPage."*
   - However, `MasterUi::handle` (`shell-mpc/overlay/master_ui.h:360-473`) does not handle `ChangeKind::ModsRequest`. Long presses on the master knobs and number fields do nothing.

4. **Single-control Q-Link Group 2 ("LINK") wastes three hardware encoders**:
   - Group 2 ("LINK", `shell-mpc/overlay/master_ui.h:482`, `shell-mpc/ui/voice_master.h:784`) assigns only `master.bend_down_link` to Q1, leaving Q2, Q3, and Q4 unassigned.
   - Allocating an entire Q-Link hardware bank to a single toggle wastes encoder capacity.

5. **`voice.phase_legato` placed on Master page instead of Voice page**:
   - `voice.phase_legato` (`shell-mpc/overlay/master_ui.h:252`) controls per-voice oscillator phase legato and shares the `voice.` parameter namespace.
   - It is located in the VOICES block on the Master page, but is completely absent from the Voice page (`shell-mpc/overlay/voice_ui.h`).

6. **Touch-only controls without Q-Link encoder representation**:
   - `quality.no_smoothing` and `quality.oversample_4x` (`shell-mpc/overlay/master_ui.h:275-276`) are engine synthesis parameters placed in the OUTPUT block strip, but have no Q-Link group assignments because Group 0 is full.
   - Similarly, shell settings `LIMIT` (`outputLimiter`, `shell-mpc/overlay/master_ui.h:274`), `PAD PRESS` (`padPressure`, `shell-mpc/overlay/master_ui.h:277`), and `PAD %` (`padAmount`, `shell-mpc/overlay/master_ui.h:278`) have no Q-Link encoder mappings and require touchscreen interaction.

7. **Redundant key selection controls for `master.scale.key`**:
   - `master.scale.key` can be selected via the `KEY` Stepper arrows (`shell-mpc/ui/base.h:703`), the `KEY` popup list (`shell-mpc/overlay/master_ui.h:428-442`), encoder Q2 (`shell-mpc/overlay/master_ui.h:486`), and the interactive `MiniKeyboardDisplay` piano keys (`shell-mpc/ui/voice_master.h:175-199`, `shell-mpc/overlay/master_ui.h:262`).
   - The interactive piano keyboard renders directly beneath the `KEY` stepper, making the stepper and its popup list redundant.
