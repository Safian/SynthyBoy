# Master UI Map
Refreshed 2026-11-23

The mapping below was checked against `MasterScreen::buildGroups` in `shell-mpc/ui/voice_master.h`. GLIDE occupies the fourth bank on MAIN. VEL TRACK, SAME NOTE and STEAL are touch-only. Physical OLED labels and encoder response remain part of the final XL check.

## Purpose
The Master page is the full-screen MPC overlay view for the "Master" tab (`shell-mpc/overlay/overlay_layout.json:786-822`, `scripts/overlay-skin/mpc-ui-pages-overlay.json:155-161`, `docs/MPC.md:1363`).
It is implemented by `MasterUi` (`shell-mpc/overlay/master_ui.h:34-539`) using `MasterScreen` (`shell-mpc/ui/voice_master.h:430-872`).
The view occupies the full tab area (1280 x 628) and provides comprehensive top-level synthesis controls organized into two switchable sides:
1. **MAIN side**:
   - **OUTPUT**: Master output volume (`master.volume`), stereo pan spread (`master.stereo_spread`), stereo routing percentage (`master.stereo_routing`), voice override behavior (`master.voice_override`), L/R peak output meters with dB scale and peak hold, output overload clip indicator, plugin lookahead output limiter, and engine rendering quality toggles (`quality.no_smoothing`, `quality.oversample_4x`).
   - **PITCH**: Master fine tuning (`master.tune`), master transposition (`master.transpose`), pitch bend wheel up range (`master.bend_range`), pitch bend wheel down range (`master.bend_down_range`, hidden while LINK is on), and bend range link switch (`master.bend_down_link`, touch only, no Q slot).
   - **VOICES**: Maximum synth polyphony (`master.polyphony`), monophonic legato (`master.legato`), polyphonic legato (`master.poly_legato`), oscillator phase legato (`voice.phase_legato`), velocity tracking sensitivity (`master.velocity_track`), same note retrigger behavior (`master.voice.same_note`), voice stealing priority rule (`master.voice.steal_priority`), sounding voice count meter, and plugin pad pressure modulation layer (PAD PRESS; PAD % only while it is on). VEL TRACK, SAME NOTE and STEAL are touch only since the Q-Link mapping extension (still visible and editable on the screen).
   - **GLIDE**: Portamento glide duration (`master.portamento.time`), glide curve slope (`master.portamento.slope`), continuous glide mode (`master.portamento.always`), interval-scaled glide time (`master.portamento.scaled`), and live portamento curvature display.
2. **TUNING & MPE side**:
   - **SCALE**: Scale selection (`master.scale.type` with 20 musical scales), root key selection (`master.scale.key` with 12 chromatic pitches), scale quantization mode (`master.scale.mode`), and interactive 12-key piano keyboard diagram.
   - **TUNING**: A4 reference pitch tuning (`master.tuning.a4`), microtonal tuning temperament system (`master.tuning.scale`), MIDI Tuning Standard ESP client support (`master.tuning.mts_esp`), and MTS Note-On filter (`master.tuning.mts_note_on_only`).
   - **MPE**: MIDI Polyphonic Expression toggle (`master.mpe.on`), per-note MPE pitch bend range (`master.mpe.bend_range`, hidden while MPE is off), MPE XYZ to macros routing (`master.mpe.xyz_macro`), MPE Y to mod wheel mapping (`master.mpe.y_modwheel`), standard pitch bend to MPE X glide mapping (`master.mpe.bend_to_x`, hidden while MPE is off), and MPE Y bipolar mode (`master.mpe.y_bipolar`).

## Layout
The Master page occupies the entire MPC overlay tab region `(0, 110, 1280, 628)` (`shell-mpc/overlay/overlay_layout.json:791-796`, `shell-mpc/overlay/master_ui.h:72,83-84`, `shell-mpc/ui/voice_master.h:539`).
The page structure consists of a top header bar and two switchable side views containing distinct functional blocks:

1. **Header Area** (y = 114 to y = 170, height 56 px, header lane 64 px):
   - Page Title: "MASTER" at `(rect.x + 14, rect.y + 4, 270, 56)` (`shell-mpc/ui/voice_master.h:812`).
   - Side Switcher Buttons:
     - `sideMain`: "MAIN" at `(rect.x + 300, rect.y + 4, 160, 56)` (`shell-mpc/ui/voice_master.h:437,695`).
     - `sideTuning`: "TUNING & MPE" at `(rect.x + 468, rect.y + 4, 220, 56)` (`shell-mpc/ui/voice_master.h:438,696`).
     - The stock Master Panel buttons highlight the selected side. They switch the view and its Q-Links without editing the preset.
   - Horizontal separator line below header at y = 172 (`shell-mpc/ui/voice_master.h:803`).

2. **Side 0: MAIN** (`shell-mpc/ui/voice_master.h:698-755`; the block rectangles are `blockRect`, `shell-mpc/ui/voice_master.h:660-674`):
   Divided into a 2x2 asymmetric block grid:
   - **Row 1 (Top, y = 182, height = 228 px)**:
     - **Block 0: OUTPUT** at `(rect.x + 8, rect.y + 72, 640, 228)` (`shell-mpc/ui/voice_master.h:699-717`):
       - Clip indicator `clip` (`ClipReadout`, width 100, height 56) at `(in.x, in.y)`.
       - Dual Peak Meter `meters` (`PeakMeterReadout`, width 100, height 128) below clip label with dB scale ticks (0, -6, -12, -24, -48 dB) and L/R peak bars.
       - Three large rotary knobs across upper area: `volume` ("VOLUME"), `spread` ("SPREAD"), and `stereoRouting` ("STEREO"), each about 119 px wide and 124 px high (computed from the layout code, not measured on a picture: unverified).
       - LED list `voiceOverride` ("VOICE OVERRIDE": Kill, Steal) at `(in.right() - 150, in.y, 150, min (upperH, 128))`.
       - Bottom strip (height 56 px = `led::kRowH`, `shell-mpc/ui/led_toggle.h:16`): LED toggle `outputLimiter` ("LIMIT", width >= 96 px), label "QUALITY" (width 70 px), LED toggle `noSmoothing` ("NO SMOOTHING"), and LED toggle `oversample4x` ("OVERSAMPLE 4X").
     - **Block 1: PITCH** at `(rect.x + 656, rect.y + 72, 616, 228)` (`shell-mpc/ui/voice_master.h:718-728`):
       - Top row: medium knob `tune` ("TUNE", width 190 px, side-dial layout) followed by three NumberFields: `transpose` ("TRANSPOSE", about 134 px), `bendUp` ("BEND UP", about 134 px) and `bendDown` ("BEND DOWN", about 134 px). While LINK is on `bendDown` is hidden (empty rectangle) and `bendUp` reads "BEND" and takes the cell of both (`shell-mpc/ui/voice_master.h:557-565,725-726`).
       - Lower strip: LED toggle `bendLink` ("LINK") positioned directly beneath `bendUp`; touch only, no Q slot (`shell-mpc/ui/voice_master.h:727`).
   - **Row 2 (Bottom, y = 418, height = 312 px)**:
     - **Block 2: VOICES** at `(rect.x + 8, rect.y + 308, 780, 312)` (`shell-mpc/ui/voice_master.h:729-746`):
       - Row 1: Four cells (width about 189 px, height 56 px): NumberField `polyphony` ("POLY") and three LED toggles `legato` ("LEGATO"), `polyLegato` ("POLY LEG"), and `phaseLegato` ("PHASE LEG").
       - Row 2: (the former VOICE OPTS group, now touch only) NumberField `velocityTrack` ("VEL TRACK", about 189 px wide, 112 px high), LED list `sameNote` ("SAME NOTE", about 226 px, 3 options: Steal, Stack, Ignore), and LED list `stealPriority` ("STEAL", 345 px, 5 options: Newest, Oldest, Highest, Lowest, Velocity).
       - Row 3: native PAD PRESS switch and PAD % number field (189 × 56 px each), plus the live overlay voice count bar (`voiceMeter`, 382 × 44 px). The MAIN subpage gate controls both native cells. PAD PRESS also gates the amount field. The overlay-only screen uses the same cells with its LED toggle and number field.
     - **Block 3: GLIDE** at `(rect.x + 796, rect.y + 308, 476, 312)` (`shell-mpc/ui/voice_master.h:747-755`):
       - Left section (about 214 px): two medium knobs `portaTime` ("GLIDE", about 105 px) and `portaSlope` ("SLOPE", about 105 px) on top; two LED toggles `portaAlways` ("ALWAYS") and `portaScaled` ("SCALED") below, 56 px high. These four are the GLIDE Q group.
       - Right section (about 246 x 268 px): Portamento curve display `portaCurve` (`PortamentoCurveDisplay`) showing exponential glide shape.

3. **Side 1: TUNING & MPE** (`shell-mpc/ui/voice_master.h:756-797`):
   Divided into three blocks:
   - **Row 1 (Top, y = 182, height = 280 px)**:
     - **Block 4: SCALE** at `(rect.x + 8, rect.y + 72, 1264, 280)` (`shell-mpc/ui/voice_master.h:757-765`):
       - Top row (height 64 px): Stepper `scaleType` ("SCALE", width 450 px), Stepper `scaleKey` ("KEY", width 325 px), and LED list `scaleMode` ("MODE", about 469 px, 2 options: Conform, Mark only).
       - Lower area (height 162 px): `keyboard` (`MiniKeyboardDisplay`, width 1252 px) featuring interactive 12-key piano keyboard (7 white keys C..B, 5 black keys) with active scale note highlights.
   - **Row 2 (Bottom, y = 470, height = 260 px)**:
     - **Block 5: TUNING** at `(rect.x + 8, rect.y + 360, 628, 260)` (`shell-mpc/ui/voice_master.h:766-774`):
       - Top row (height about 144 px): NumberField `tuningA4` ("A4 HZ", width 200 px) and LED list `tuningScale` ("TUNING", about 412 px, 4 options: 12-TET, Pythagorean, Just intonation, User file).
       - Bottom row (height 56 px): LED toggle `mtsEsp` ("MTS-ESP", about 306 px) and LED toggle `mtsNoteOn` ("NOTE-ON", about 306 px).
     - **Block 6: MPE** at `(rect.x + 644, rect.y + 360, 628, 260)` (`shell-mpc/ui/voice_master.h:775-797`):
       - Top row (height about 144 px): LED toggle `mpeOn` ("MPE", about 202 px), NumberField `mpeBend` ("MPE BEND", about 202 px, hidden while MPE is off), and LED list `mpeMacro` ("XYZ TO MACROS", about 204 px, options: Off, XYZ, YZ). With MPE off the remaining two cells are about 306 px each (`shell-mpc/ui/voice_master.h:788-796`).
       - Bottom row (height 56 px): LED toggle `mpeYModwheel` ("Y TO WHEEL"), LED toggle `mpeBendToX` ("BEND TO X", hidden while MPE is off), and LED toggle `mpeYBipolar` ("Y BIPOLAR"), about 202 px each (MPE off: two of about 306 px).

## Controls
In the gesture column `Qn` is slot n INSIDE the control's Q group; where the group sits on the knobs (XL slots, Key 61 banks) is under "Q-Link groups". Controls marked touch only have no Q slot.

| Label on screen | Parameter key | Gesture | Effect | Source |
|---|---|---|---|---|
| MAIN | - | Tap | Switches view to the Main side (OUTPUT, PITCH, VOICES, GLIDE blocks) | `shell-mpc/ui/voice_master.h:641-643` |
| TUNING & MPE | - | Tap | Switches view to the Tuning & MPE side (SCALE, TUNING, MPE blocks) | `shell-mpc/ui/voice_master.h:644` |
| CLIP | - | Tap | Clears sticky output clip overload warning flag | `shell-mpc/overlay/master_ui.h:413-417` |
| VOLUME | `master.volume` | Rotary drag / Wheel / Q1 turn / Tap value | Sets master output volume (-60.0 to +6.0 dB, tap value opens text keyboard) | `shell-mpc/overlay/master_ui.h:243` |
| SPREAD | `master.stereo_spread` | Rotary drag / Wheel / Q2 turn / Tap value | Sets stereo pan spread across voices (0% to 100%, tap value opens text keyboard) | `shell-mpc/overlay/master_ui.h:244` |
| STEREO | `master.stereo_routing` | Rotary drag / Wheel / Q3 turn / Tap value | Sets stereo routing crossmix percentage (0% to 100%, tap value opens text keyboard) | `shell-mpc/overlay/master_ui.h:245` |
| VOICE OVERRIDE | `master.voice_override` | Tap option / Wheel / Q4 turn | Sets voice allocation override mode (Kill, Steal) | `shell-mpc/overlay/master_ui.h:246` |
| LIMIT | - | Tap toggle (touch only) | Toggles plugin output lookahead peak limiter on / off (shell setting) | `shell-mpc/overlay/master_ui.h:277` |
| NO SMOOTHING | `quality.no_smoothing` | Tap toggle (touch only) | Disables parameter interpolation smoothing across blocks (Off / On) | `shell-mpc/overlay/master_ui.h:278` |
| OVERSAMPLE 4X | `quality.oversample_4x` | Tap toggle (touch only) | Enables 4x internal oversampling for synthesis engine (Off / On) | `shell-mpc/overlay/master_ui.h:279` |
| TUNE | `master.tune` | Rotary drag / Wheel / Q1 turn / Tap value | Sets master fine tuning (-100 to +100 cents, tap value opens text keyboard) | `shell-mpc/overlay/master_ui.h:247` |
| TRANSPOSE | `master.transpose` | Vertical drag / Wheel / Q2 turn | Sets master pitch transposition (-48 to +48 semitones) | `shell-mpc/overlay/master_ui.h:248` |
| BEND UP | `master.bend_range` | Vertical drag / Wheel / Q3 turn | Sets pitch bend wheel up range (0 to 24 semitones) | `shell-mpc/overlay/master_ui.h:249` |
| BEND DOWN | `master.bend_down_range` | Vertical drag / Wheel / Q4 turn (PITCH group, only while LINK is off) | Sets pitch bend wheel down range (0 to 24 semitones); hidden (no rectangle, no touch, no Q slot) while LINK is on | `shell-mpc/overlay/master_ui.h:250` |
| LINK | `master.bend_down_link` | Tap toggle (touch only, no Q slot since the bold cut) | Links pitch bend down range to pitch bend up range (Off / On); while On BEND DOWN is hidden and BEND UP reads BEND | `shell-mpc/overlay/master_ui.h:251` |
| POLY | `master.polyphony` | Vertical drag / Wheel / Q1 turn | Sets maximum synth polyphony (1 to 32 voices) | `shell-mpc/overlay/master_ui.h:252` |
| LEGATO | `master.legato` | Tap toggle / Q2 turn | Enables monophonic legato envelope retriggering (Off / On) | `shell-mpc/overlay/master_ui.h:253` |
| POLY LEG | `master.poly_legato` | Tap toggle / Q3 turn | Enables polyphonic legato envelope retriggering (Off / On) | `shell-mpc/overlay/master_ui.h:254` |
| PHASE LEG | `voice.phase_legato` | Tap toggle / Q4 turn | Preserves oscillator phase on legato note transitions (Off / On) | `shell-mpc/overlay/master_ui.h:255` |
| VEL TRACK | `master.velocity_track` | Vertical drag / Wheel (touch only, no Q slot) | Sets velocity tracking sensitivity (-100% to +100%) | `shell-mpc/overlay/master_ui.h:258` |
| SAME NOTE | `master.voice.same_note` | Tap option / Wheel (touch only, no Q slot) | Sets voice behavior when same note is retriggered (Steal, Stack, Ignore) | `shell-mpc/overlay/master_ui.h:256` |
| STEAL | `master.voice.steal_priority` | Tap option / Wheel (touch only, no Q slot) | Sets voice stealing priority rule (Newest, Oldest, Highest, Lowest, Velocity) | `shell-mpc/overlay/master_ui.h:257` |
| PAD PRESS | - | Native tap or Enter toggle | Enables the shell pad pressure layer. The stored amount survives switching it off. | `scripts/native_master/master_controls.py` |
| PAD % | - | Native number entry or focused wheel | Sets the pad pressure amount (0–100%) while PAD PRESS is on. | `scripts/native_master/master_controls.py` |
| GLIDE | `master.portamento.time` | Rotary drag / Wheel / Q1 turn / Tap value | Sets portamento glide duration (0 to 8000 ms, tap value opens text keyboard) | `shell-mpc/overlay/master_ui.h:259` |
| SLOPE | `master.portamento.slope` | Rotary drag / Wheel / Q2 turn / Tap value | Sets portamento glide slope curvature (-8.0 concave to +8.0 convex, tap value opens text keyboard) | `shell-mpc/overlay/master_ui.h:260` |
| ALWAYS | `master.portamento.always` | Tap toggle / Q3 turn | Enables portamento glide on all notes, not just legato overlaps (Off / On) | `shell-mpc/overlay/master_ui.h:261` |
| SCALED | `master.portamento.scaled` | Tap toggle / Q4 turn | Scales portamento glide time by pitch interval distance (Off / On) | `shell-mpc/overlay/master_ui.h:262` |
| SCALE | `master.scale.type` | Tap arrows / Turn / Q1 turn / Tap caption | Selects active musical scale (20 options; tap caption opens list popup) | `shell-mpc/overlay/master_ui.h:263` |
| KEY | `master.scale.key` | Tap arrows / Turn / Q2 turn / Tap caption | Selects musical root key (12 options C..B; tap caption opens list popup) | `shell-mpc/overlay/master_ui.h:264` |
| MODE | `master.scale.mode` | Tap option / Wheel / Q3 turn | Sets scale quantization mode (Conform, Mark only) | `shell-mpc/overlay/master_ui.h:266` |
| Scale Keyboard | `master.scale.key` | Tap or drag piano key | Selects musical root key (0..11, C..B) by touching piano keys on mini keyboard | `shell-mpc/overlay/master_ui.h:265` |
| A4 HZ | `master.tuning.a4` | Vertical drag / Wheel / Q1 turn | Sets reference concert pitch frequency for A4 (400.00 to 480.00 Hz in 0.01 Hz steps) | `shell-mpc/overlay/master_ui.h:267` |
| TUNING | `master.tuning.scale` | Tap option / Wheel / Q2 turn | Selects microtonal tuning temperament system (12-TET, Pythagorean, Just intonation, User file) | `shell-mpc/overlay/master_ui.h:265` |
| MTS-ESP | `master.tuning.mts_esp` | Tap toggle / Q3 turn | Enables MIDI Tuning Standard ESP client synchronization (Off / On) | `shell-mpc/overlay/master_ui.h:269` |
| NOTE-ON | `master.tuning.mts_note_on_only` | Tap toggle / Q4 turn | Restricts MTS tuning updates to Note-On events only (Off / On) | `shell-mpc/overlay/master_ui.h:270` |
| MPE | `master.mpe.on` | Tap toggle / Q1 turn | Enables MIDI Polyphonic Expression MPE mode (Off / On) | `shell-mpc/overlay/master_ui.h:271` |
| MPE BEND | `master.mpe.bend_range` | Vertical drag / Wheel / Q2 turn (only while MPE is on) | Sets per-note MPE pitch bend range (1 to 96 semitones); hidden while MPE is off | `shell-mpc/overlay/master_ui.h:272` |
| XYZ TO MACROS | `master.mpe.xyz_macro` | Tap option / Wheel / Q3 turn (Q2 while MPE is off) | Routes MPE X/Y/Z expressive dimensions to Macros (Off, XYZ, YZ) | `shell-mpc/overlay/master_ui.h:273` |
| Y TO WHEEL | `master.mpe.y_modwheel` | Tap toggle / Q1 turn | Maps MPE Y dimension (CC74 timbre) to standard mod wheel CC1 (Off / On) | `shell-mpc/overlay/master_ui.h:274` |
| BEND TO X | `master.mpe.bend_to_x` | Tap toggle / Q2 turn (only while MPE is on) | Maps standard pitch bend to MPE X horizontal glide dimension (Off / On); hidden while MPE is off | `shell-mpc/overlay/master_ui.h:275` |
| Y BIPOLAR | `master.mpe.y_bipolar` | Tap toggle / Q3 turn (Q2 while MPE is off) | Treats MPE Y dimension as bipolar centered at 64 (-1.0 to +1.0) instead of unipolar (Off / On) | `shell-mpc/overlay/master_ui.h:276` |

## Opens

The release skin opens the MPC `MenuOverlay` by touching a selector, and the MPC `NumericOverlay` by touching a whole-number field. Double tap a dial to enter its value. Enter opens the same host menu on a focused control. The host handles the real parameter and its value range. The overlay suspends input and Q-Links while the menu owns the display. Closing it restores the active Master side.

In the overlay-only view used by the UI component tests and gallery, `MasterUi` has its own `UiStack` details:

1. **List Selection Popup (`ui::ListPopup list_`, `kPageList = 15`)**:
   - **Trigger**: Tapping the caption or center zone of either stepper: `scaleType` ("SCALE") or `scaleKey` ("KEY") (`shell-mpc/overlay/master_ui.h:433-447`). Both steppers are configured with `listable = true` (`shell-mpc/overlay/master_ui.h:282-283`), causing center taps to emit `ChangeKind::Request`.
   - **Bounds**: Starts at `(340, 130)` and is 600 px wide. The height fits eight list rows and the vertical padding.
   - **Controls inside popup**:
     | Label on screen | Parameter key | Gesture | Effect | Source |
     |---|---|---|---|---|
     | Choice Item | `master.scale.type` or `master.scale.key` | Tap row / Vertical scroll drag | Selects option, writes plain index to active stepper parameter, and closes popup | `shell-mpc/overlay/master_ui.h:374-380` |
     | Backdrop | - | Tap outside popup box | Dismisses list popup without changing parameter (`kListBackdrop = 701`) | `shell-mpc/overlay/master_ui.h:369-373` |

2. **Virtual Numeric Keyboard (`ui::TextEntry entry_`, `kPageKeyboard = 16`)**:
   - **Trigger**: Tapping the numeric value readout of any Knob on the page (`volume`, `spread`, `stereoRouting`, `tune`, `portaTime`, `portaSlope`). Tapping the value readout emits `ChangeKind::Request` (`shell-mpc/ui/base.h:441-445`), which opens the text entry page (`shell-mpc/overlay/master_ui.h:448-463`).
   - **Bounds**: Full tab area `(0, 110, 1280, 628)` (`shell-mpc/overlay/master_ui.h:86`, `entry_.place (tab)`).
   - **Controls inside keyboard**:
     | Label on screen | Parameter key | Gesture | Effect | Source |
     |---|---|---|---|---|
     | Keypad Buttons | - | Tap key (0..9, `-`, `.`) | Appends digit or punctuation symbol to numeric input buffer | `shell-mpc/ui/text_entry.h:113` |
     | Backspace | - | Tap key | Deletes previous character in input buffer | `shell-mpc/ui/text_entry.h:128` |
     | Layer Switch | - | Tap key | Toggles between letters and symbols keypad layers | `shell-mpc/ui/text_entry.h:139` |
     | CANCEL | - | Tap button | Closes virtual keyboard without applying changes (`kKeyboardCancel = 711`) | `shell-mpc/overlay/master_ui.h:381-385` |
     | OK | - | Tap button | Parses entered text via `strtod`, clamps to parameter range, rounds to 1 decimal place, writes value to the target knob parameter, and closes keyboard (`kKeyboardOk = 710`) | `shell-mpc/overlay/master_ui.h:386-405` |

3. **Modulation Pages**:
   - Not opened. Even though `Knob::tick` and `NumberField::tick` emit `ChangeKind::ModsRequest` on a 600 ms long press (`shell-mpc/ui/base.h:465,656`), `MasterUi::handle` (`shell-mpc/overlay/master_ui.h:365-478`) does not implement a handler for `ChangeKind::ModsRequest`. Long presses on knobs or number fields have no effect.

## Q-Link groups
The page has 9 Q-Link groups across its two sides (`MasterScreen::kMaxGroups = 9`, `kFirstTuningGroup = 5`, `shell-mpc/ui/voice_master.h:433,480-481`; members built by `buildGroups`, `shell-mpc/ui/voice_master.h:840-871`). A group is its LIVE members from slot 1 on: a hidden control or a touch-only control is no member and the group closes up (`close`, `shell-mpc/ui/voice_master.h:842-849`). The side that is up decides which five (MAIN) or four (TUNING & MPE) groups exist; groups without members are not published (`usedGroups`, `shell-mpc/overlay/q_publish.h:21-33`). Pictures: `docs/screens/master.jpg`, `docs/ui-map/mixer-master-bold-cut/after/`.

### Side MAIN (groups 0 to 4; four of them have members)
| Group | Q1 | Q2 | Q3 | Q4 |
| --- | --- | --- | --- | --- |
| 0 OUTPUT (`shell-mpc/ui/voice_master.h:850`) | `master.volume` | `master.stereo_spread` | `master.stereo_routing` | `master.voice_override` |
| 1 PITCH (`shell-mpc/ui/voice_master.h:851-854`) | `master.tune` | `master.transpose` | `master.bend_range` | `master.bend_down_range` (LINK off) or unassigned (LINK on) |
| 2 VOICES (`shell-mpc/ui/voice_master.h:855`) | `master.polyphony` | `master.legato` | `master.poly_legato` | `voice.phase_legato` |
| 3 VOICE OPTS (`shell-mpc/ui/voice_master.h:1035`) | no members: `master.velocity_track`, `master.voice.same_note`, `master.voice.steal_priority` are touch only | | | |
| 4 GLIDE (`shell-mpc/ui/voice_master.h:1036`) | `master.portamento.time` | `master.portamento.slope` | `master.portamento.always` | `master.portamento.scaled` |

Change of the Q-Link mapping extension (`docs/ui-map/q-link-audit.md` item 7, "owner may reverse"): GLIDE replaces VOICE OPTS on the Q-Links so that four groups fit exactly; the group object VOICE OPTS stays in the table with no members (one line to reverse, `shell-mpc/ui/voice_master.h:1035`). `master.bend_down_link` (LINK) has been no member since the bold cut.

### Side TUNING & MPE (groups 5 to 8)
| Group | Q1 | Q2 | Q3 | Q4 |
| --- | --- | --- | --- | --- |
| 5 SCALE (`shell-mpc/ui/voice_master.h:858`) | `master.scale.type` | `master.scale.key` | `master.scale.mode` | unassigned |
| 6 TUNING (`shell-mpc/ui/voice_master.h:859`) | `master.tuning.a4` | `master.tuning.scale` | `master.tuning.mts_esp` | `master.tuning.mts_note_on_only` |
| 7 MPE (`shell-mpc/ui/voice_master.h:860-868`) | `master.mpe.on` | `master.mpe.bend_range` (MPE on) | `master.mpe.xyz_macro` | unassigned; with MPE off: [`master.mpe.on`, `master.mpe.xyz_macro`] |
| 8 MPE Y (`shell-mpc/ui/voice_master.h:860-868`) | `master.mpe.y_modwheel` | `master.mpe.bend_to_x` (MPE on) | `master.mpe.y_bipolar` | unassigned; with MPE off: [`master.mpe.y_modwheel`, `master.mpe.y_bipolar`] |

### Where the groups sit on the knobs
`MasterUi::publishQGroup` (`shell-mpc/overlay/master_ui.h:490-516`): the groups of the side that is up (`first = 0, count = 5` on MAIN, `first = 5, count = 4` on TUNING & MPE), the ones with members in reading order, mapped by `QSlots::publish` (`shell-mpc/overlay/q_publish.h:57-65`). While a detail page (list or keyboard) is open nothing is published (`shell-mpc/overlay/master_ui.h:495,500-501`).
- **MPC XL (16 knobs, no active group).** Four used groups on each side, so each keeps its slots (`QRouter::blockOfGroup`, `shell-mpc/q_router.h:71-79`): MAIN Q1 .. 4 OUTPUT, Q5 .. 8 PITCH, Q9 .. 12 VOICES, Q13 .. 16 GLIDE (Q8 is empty while LINK is on). TUNING & MPE: Q1 .. 4 SCALE, Q5 .. 8 TUNING, Q9 .. 12 MPE, Q13 .. 16 MPE Y. The XL draws no frame; the chips carry 1 .. 16.
- **Key 61 (four knobs, banks).** Bank n = Q 4n-3 .. 4n of the same list: the ACTIVE group (the last touched) is bank 1, the others follow in reading order and wrap, so MAIN has four banks (OUTPUT, PITCH, VOICES, GLIDE), TUNING & MPE four (SCALE, TUNING, MPE, MPE Y). A frame marks the active group.
- Not on any Q-Link (touch only): LINK, VEL TRACK, SAME NOTE, STEAL, LIMIT, QUALITY pair (`quality.no_smoothing`, `quality.oversample_4x`), PAD PRESS, PAD %, the Scale Keyboard, the side switches.

### Activation and drawing
- On four-knob devices, touching a native control selects its Q-Link group while the MPC handles the edit. A touch-only control selects the group of its block. MAIN opens on OUTPUT and TUNING opens on SCALE. On the XL, the active side's groups keep their assigned slots.
- The active group draws an accent frame, the inactive groups of the side draw idle chips (`QGroup::draw`, `QGroup::drawIdle`, `shell-mpc/ui/voice_master.h:648-656`, `shell-mpc/ui/qgroup.h:75,123`).
- `PAD %`, `PAD PRESS`, `LIMIT` and the QUALITY pair were never members (shell settings or engine parameters left without a slot: `shell-mpc/overlay/master_ui.h:277-281`).

## Live parts
1. **Master Peak Meters**. The native MAIN skin has separate vertical L/R Meter widgets and a dB scale. `MasterUi` reads the engine's two channel peaks during its ordinary frame update. Each channel has a hold line descending at 30 dB/s. The Mixer continues to show the greater channel level on its MAIN strip.
2. **CLIP**. The red warning stays on for two seconds after a safety-stage hit. Tap or press Enter to clear the current instance's warning. The overlay-only screen uses the same timed state.
3. **Voice count**. The native horizontal Meter reads the sounding-voice fraction and shows the current voice count and polyphony limit. The overlay-only screen keeps its segmented bar.

4. **Portamento Glide Curve Diagram (`portaCurve`, `PortamentoCurveDisplay`)**:
   - In `MasterUi::update` (`shell-mpc/overlay/master_ui.h:126`), `master.portamento.slope` is read from `inst.param(slopeIdx_)`.
   - In `PortamentoCurveDisplay::drawBody` (`shell-mpc/ui/voice_master.h:36-73`), a 32-segment exponential curve $y = (e^{k \cdot t} - 1) / (e^k - 1)$ is dynamically calculated and drawn in real time across the grid, visualizing linear (slope = 0), concave (slope < 0), or convex (slope > 0) glide acceleration.

5. **Scale Mini Keyboard Diagram (`keyboard`, `MiniKeyboardDisplay`)**:
   - In `MasterUi::update` (`shell-mpc/overlay/master_ui.h:127-129`), `master.scale.type`, `master.scale.key`, and `master.scale.mode` are refreshed.
   - In `MiniKeyboardDisplay::drawBody` (`shell-mpc/ui/voice_master.h:203-246`), all 12 chromatic keys are rendered. In-scale notes are highlighted with light backgrounds and glowing outlines, while the root pitch receives a solid flow accent indicator bar. The header caption dynamically reflects the active scale root: `"ALL 12 NOTES"` for chromatic or `"SCALE ON <KEY>"` for chosen scales.

6. **Active Q-Link Group Outline and Idle Chips**:
   - The active Q-Link group draws a bright accent frame around its controls (`QGroup::draw`, `shell-mpc/ui/voice_master.h:651-652`; not drawn on the XL).
   - Inactive groups on the visible side draw quiet idle chips with slot numbers (`QGroup::drawIdle`, `shell-mpc/ui/voice_master.h:653-655`).
7. **Output limiter and pad pressure state** (`shell-mpc/overlay/master_ui.h:108-125`): LIMIT shows the stage's real state, PAD PRESS the plugin's; PAD % is shown only while PAD PRESS is on.

## Standalone Master Tab Migration to Host-Native MPC Controls

The MPC skin supplies the Master dials, toggles, selectors, and number fields. It draws the output and voice meters, CLIP and LIMIT on MAIN. The overlay draws the curves, keyboard and its detail overlays.

### 1. Architecture & Ownership
- **Master-specific source files**: `shell-mpc/ui/voice_master.h` (`isNativeControl`, `isRetainedOverlayControl`, and transparent scene rendering), `shell-mpc/overlay/master_ui.h` (`covers()` passthrough, scene dispatch), `shell-mpc/overlay/tests/mastervoice_ui_host.cpp` (`testStandaloneMasterNativeControls`).
- **Dedicated auto-loaded skin extra**: `scripts/mpc-skin-extras/75_master.py` and `scripts/native_master/master_controls.py`. Automatically loaded by `scripts/apply-mpc-skin-extras.py`.
- **Unit test suite**: `scripts/tests/test_native_master.py` (verifies the engine and shell bindings, bounds, and widget types in the SDK-generated skin).

### 2. Host-Native Standard Controls
All standard synthesis parameters on the Master tab are converted to native MPC controls with exact geometry matching `MasterScreen::layout()`:
- **OUTPUT block (SideMain)**:
  - `master.volume` (bipolar dial, dB scale, rect: 124 206 116 136)
  - `master.stereo_spread` (unipolar dial, % scale, rect: 246 206 116 136)
  - `master.stereo_routing` (unipolar dial, % scale, rect: 368 206 116 136)
  - `master.voice_override` (selector: Kill, Steal, rect: 490 206 150 128)
  - `quality.no_smoothing` (toggle, rect: 308 346 120 56)
  - `quality.oversample_4x` (toggle, rect: 434 346 140 56)
- **PITCH block (SideMain)**:
  - `master.tune` (bipolar dial, cents, rect: 664 206 190 124)
  - `master.transpose` (bipolar dial/number, semitones, rect: 860 206 130 124)
  - `master.bend_range` (unipolar dial, semitones, rect: 996 206 130 124)
  - `master.bend_down_range` (unipolar dial, semitones, rect: 1132 206 132 124)
  - `master.bend_down_link` (toggle, rect: 996 346 96 56)
- **VOICES block (SideMain)**:
  - `master.polyphony` (unipolar dial/number, 1..32 voices, rect: 16 442 186 56)
  - `master.legato` (toggle, rect: 208 442 186 56)
  - `master.poly_legato` (toggle, rect: 400 442 186 56)
  - `voice.phase_legato` (toggle, rect: 592 442 188 56)
  - `master.velocity_track` (bipolar dial, % scale, rect: 16 514 186 112)
  - `master.voice.same_note` (selector: Steal, Stack, Ignore, rect: 208 514 221 112)
  - `master.voice.steal_priority` (selector: Newest, Oldest, Highest, Lowest, Velocity, rect: 435 514 345 112)
- **GLIDE block (SideMain)**:
  - `master.portamento.time` (unipolar dial, ms, rect: 804 442 102 208)
  - `master.portamento.slope` (bipolar dial, curvature, rect: 912 442 103 208)
  - `master.portamento.always` (toggle, rect: 804 666 102 56)
  - `master.portamento.scaled` (toggle, rect: 912 666 103 56)
- **SCALE block (SideTuning)**:
  - `master.scale.type` (stepper/selector: 20 scales, rect: 16 206 449 64)
  - `master.scale.key` (stepper/selector: 12 keys C..B, rect: 471 206 324 64)
  - `master.scale.mode` (selector: Conform, Mark only, rect: 801 206 463 64)
- **TUNING block (SideTuning)**:
  - `master.tuning.a4` (dial, Hz, rect: 16 494 200 156)
  - `master.tuning.scale` (selector: 12-TET, Pythagorean, Just, User, rect: 222 494 406 156)
  - `master.tuning.mts_esp` (toggle, rect: 16 666 303 56)
  - `master.tuning.mts_note_on_only` (toggle, rect: 325 666 303 56)
- **MPE block (SideTuning)**:
  - `master.mpe.on` (toggle, rect: 652 494 200 156)
  - `master.mpe.bend_range` (unipolar dial, semitones, rect: 858 494 200 156)
  - `master.mpe.xyz_macro` (selector: Off, XYZ, YZ, rect: 1064 494 200 104)
  - `master.mpe.y_modwheel` (toggle, rect: 652 666 200 56)
  - `master.mpe.bend_to_x` (toggle, rect: 858 666 200 56)
  - `master.mpe.y_bipolar` (toggle, rect: 1064 666 200 56)

### 3. Explicit List of Unconverted Controls (Retained in Overlay)
The following controls are deliberately retained in the overlay layer:

| Control | Component | Region | Reason for Retaining in Overlay |
|---|---|---|---|
| Side Switcher MAIN | `MenuButton` | `(300, 114, 160, 56)` | Page navigation button switching between MAIN and TUNING & MPE sub-views. |
| Side Switcher TUNING & MPE | `MenuButton` | `(468, 114, 220, 56)` | Page navigation button switching between MAIN and TUNING & MPE sub-views. |
| Block Headers | `isBlockHeader` | Title strips `(x, y, w, 38)` | Touch targets allowing tapping on a block title to switch active Q-Link group in native mode. |
| Glide Slope Curve | `PortamentoCurveDisplay` | `(1021, 442, 243, 280)` | 32-point mathematical exponential curve plotting real-time transition curve from `master.portamento.slope`. |
| Scale Keyboard | `MiniKeyboardDisplay` | `(16, 280, 1248, 174)` | Interactive 12-key piano keyboard diagram with dynamic in-scale highlighting and note tap selection. |
| Choice Popup | `ListPopup` | `(340, 130, 600, 460)` | Modal detail popup overlay for selecting among 20 musical scales and 12 root keys. |
| Virtual Keypad | `TextEntry` | `(0, 110, 1280, 628)` | Modal numeric keypad popup opening on knob taps for direct value entry. |

### 4. Touch Passthrough Protocol & Subpage Exclusivity
- `MasterUi::covers(x, y)`:
  - Outside the tab region `(0, 110, 1280, 628)`: returns `false` (touches fall through to host framework).
  - Modal detail popups (`pageTop_ != 0`): returns `true` (overlay handles/covers all tab interaction).
  - Native mode (`full_.nativeControls()`): returns `false` ONLY when `full_.isNativeControl(x, y)` is true. Otherwise returns `true`.
- **Subpage Exclusivity**:
  - While on `SideMain`: only active `SideMain` native controls yield `isNativeControl(x, y) == true` and pass touches through to the host. Any touch on `SideTuning` coordinates is absorbed by the overlay (`covers() == true`), preventing unintended interactions with inactive subpage controls underneath.
  - While on `SideTuning`: only active `SideTuning` native controls yield `isNativeControl(x, y) == true` and pass touches through. Any touch on `SideMain` coordinates is absorbed by the overlay (`covers() == true`).
  - Both navigation buttons (`sideMain` and `sideTuning`) are retained overlay controls; they remain visible, highlighted, and interactive on both sides.
- **Touch Target Identity**:
  - `isNativeControl(x, y)` checks exact component rectangles of live controls rather than broad block areas. Gaps between controls, block padding, and conditional disabled controls (`bendDown` when `bendLinked_`, `mpeBend`/`mpeBendToX` when `!mpeOn_`) are absorbed by the overlay.
- **Caption Separation**:
  - Each converted native control carries a single distinct uppercase caption via `WithName[<CAPTION>]/Parameter <idx>` in its handle remapping.
  - `apply_master_native_controls` purges legacy fallback controls from `TUI.json` (such as stock fallback `Master Transpose` from `master_voicing`), preventing ghost control collisions and duplicate overlapping text.
- Scene rendering transparency: in native mode, `MasterUi::draw` leaves dial interiors transparent so native host controls show through without visual obstruction while painting retained overlay widgets.

### 5. Replacement Strings and Conditional Enabling for Native Widgets
- **Selector Choices (`pageData.replacementStrings`)**: All 8 discrete selectors on the Master tab (`master.voice_override`, `master.voice.same_note`, `master.voice.steal_priority`, `master.scale.type`, `master.scale.key`, `master.scale.mode`, `master.tuning.scale`, `master.mpe.xyz_macro`) have their option text mapped into `tui["pageData"]["replacementStrings"]` keyed by parameter string `"Parameter %d" % idx`. This ensures host MPC focus menus (`sliderWithFocusMenu`) populate valid item names rather than opening empty menus.
- **Conditional Enabling (`IndexedEnabling`)**:
  - `master.bend_down_range`: Disabled/hidden when `master.bend_down_link` is ON (1), enabled when OFF (0).
  - `master.mpe.bend_range`: Enabled only when `master.mpe.on` is ON (1).
  - `master.mpe.bend_to_x`: Enabled only when `master.mpe.on` is ON (1).
- **Overlay Gating Support**: When overlay gating is present, `RangedEnabling/0/18/33/Parameter <overlay_param>` is attached so host widgets are inactive when an overlay popup is open.

### 5. Hardware Q-Link Routing
- Hardware encoder mappings via `publishQGroup` remain active for all 9 groups:
  - XL: all 4 groups on the active side (16 encoders) are mapped simultaneously to parameters.
  - Key 61: 4 encoders mapped to the active group, with banking across groups.

## Duplicates
- `master.volume`: Also appears on the Mixer overlay page (`MixerUi`, `shell-mpc/overlay/mixer_ui.h:330`) as the primary level fader of strip 9 ("MASTER").
- Note on `voice.phase_legato`: Carries the `voice.` prefix but is bound exclusively on the Master page (`shell-mpc/overlay/master_ui.h:255`) and is absent from the Voice page (`shell-mpc/overlay/voice_ui.h`).
- None of the other parameter keys bound on this page (`master.stereo_spread`, `master.stereo_routing`, `master.voice_override`, `master.tune`, `master.transpose`, `master.bend_range`, `master.bend_down_range`, `master.bend_down_link`, `master.polyphony`, `master.legato`, `master.poly_legato`, `master.voice.same_note`, `master.voice.steal_priority`, `master.velocity_track`, `master.portamento.time`, `master.portamento.slope`, `master.portamento.always`, `master.portamento.scaled`, `master.scale.type`, `master.scale.key`, `master.scale.mode`, `master.tuning.a4`, `master.tuning.scale`, `master.tuning.mts_esp`, `master.tuning.mts_note_on_only`, `master.mpe.on`, `master.mpe.bend_range`, `master.mpe.xyz_macro`, `master.mpe.y_modwheel`, `master.mpe.bend_to_x`, `master.mpe.y_bipolar`, `quality.no_smoothing`, `quality.oversample_4x`) appear on any other overlay page or tab under `shell-mpc/`.

## Questionable
1. **NumberField controls write step index instead of parameter value (Bug)**:
   - In `shell-mpc/overlay/master_ui.h:472-474`, `handle()` processes `ChangeKind::Index` with:
     ```cpp
     case ui::ChangeKind::Index:
         uibind::writePlain (inst, it->param, ch.index);
         break;
     ```
   - When a user touches or drags any `NumberField` (`transpose`, `bendUp`, `bendDown`, `polyphony`, `velocityTrack`, `tuningA4`, `mpeBend`), `NumberField` emits `Change::indexed (id_, idx, state.min + idx * stepSize())` (`shell-mpc/ui/base.h:665`).
   - The member ch.index contains the 0-based step index (`idx`), while ch.value contains the actual calculated value (`state.min + idx * stepSize()`).
   - Writing ch.index corrupts parameter values whenever fields are dragged or turned:
     - `master.transpose` (range -48 to +48): index 48 (center/0 st) writes 48 semitones to the engine instead of 0.
     - `master.tuning.a4` (range 400.00 to 480.00 Hz, step 0.01): index 0..8000 writes 0..8000 Hz instead of 400..480 Hz.
     - `master.velocity_track` (range -100 to +100%): index 100 (0%) writes 100% sensitivity.
     - `master.polyphony` (range 1 to 32): 1 voice has index 0, writing 0 voices to the engine instead of 1.
   - Note that `padAmount` in `shell-mpc/overlay/master_ui.h:426-427` specifically used ch.value to avoid this bug (`inst.setPadPressure (on, static_cast<float> (ch.value));`), but the general item handler failed to do so.

2. **Knob virtual keyboard dialog title hardcodes "dB" unit for all parameters (Cosmetic/Confusing)**:
   - In `shell-mpc/overlay/master_ui.h:458`:
     ```cpp
     std::snprintf (title, sizeof title, "%s (%.1f .. %.1f dB)", pi.name, pi.min, pi.max);
     ```
   - Tapping the numeric readout of any Knob opens `entry_` to allow typing a value. However, the title string unconditionally appends `"dB"` regardless of the parameter's actual unit.
   - For example, tapping `TUNE` displays `"Master Tune (-100.0 .. 100.0 dB)"` (actual unit: cents), and tapping `GLIDE` displays `"Portamento Time (0.0 .. 8000.0 dB)"` (actual unit: ms).

3. **Unhandled `ChangeKind::ModsRequest` (Unwired modulation details)**:
   - `Knob::tick` (`shell-mpc/ui/base.h:465`) and `NumberField::tick` (`shell-mpc/ui/base.h:656`) emit `ChangeKind::ModsRequest` after a 600 ms long press.
   - `docs/MPC.md:1369` states: *"All elements bind to engine parameters with ui_bind.h ... and open the modulations of a control (long press) and the sources' target list with the shared ModulationsPage / ModTargetsPage."*
   - However, `MasterUi::handle` (`shell-mpc/overlay/master_ui.h:365-478`) does not handle `ChangeKind::ModsRequest`. Long presses on the master knobs and number fields do nothing.

4. FIXED (bold cut): **Single-control Q-Link Group 2 ("LINK") wasted three hardware encoders** (LINK is now touch only; the group is gone). Old finding:
   - Group 2 ("LINK", gone since the bold cut) assigns only `master.bend_down_link` to Q1, leaving Q2, Q3, and Q4 unassigned.
   - Allocating an entire Q-Link hardware bank to a single toggle wastes encoder capacity.

5. **`voice.phase_legato` placed on Master page instead of Voice page**:
   - `voice.phase_legato` (`shell-mpc/overlay/master_ui.h:255`) controls per-voice oscillator phase legato and shares the `voice.` parameter namespace.
   - It is located in the VOICES block on the Master page, but is completely absent from the Voice page (`shell-mpc/overlay/voice_ui.h`).

6. **Touch-only controls without Q-Link encoder representation**:
   - `quality.no_smoothing` and `quality.oversample_4x` (`shell-mpc/overlay/master_ui.h:278-279`) are engine synthesis parameters placed in the OUTPUT block strip, but have no Q-Link group assignments because Group 0 is full.
   - Similarly, shell settings `LIMIT` (`outputLimiter`, `shell-mpc/overlay/master_ui.h:277`), `PAD PRESS` (`padPressure`, `shell-mpc/overlay/master_ui.h:280`), and `PAD %` (`padAmount`, `shell-mpc/overlay/master_ui.h:281`) have no Q-Link encoder mappings and require touchscreen interaction.

7. **Redundant key selection controls for `master.scale.key`**:
   - `master.scale.key` can be selected via the `KEY` Stepper arrows (`shell-mpc/ui/base.h:739-747`), the `KEY` popup list (`shell-mpc/overlay/master_ui.h:433-447`), encoder Q2 of the SCALE group (`shell-mpc/ui/voice_master.h:858`), and the interactive `MiniKeyboardDisplay` piano keys (`shell-mpc/ui/voice_master.h:176-201`, `shell-mpc/overlay/master_ui.h:265`).
   - The interactive piano keyboard renders directly beneath the `KEY` stepper, making the stepper and its popup list redundant.

8. **GLIDE replaced VOICE OPTS on the Q-Links (open for the owner)**: VEL TRACK, SAME NOTE and STEAL are visible controls with no Q-Link (`shell-mpc/ui/voice_master.h:1035`); the audit marks the choice "owner may reverse" (`docs/ui-map/q-link-audit.md`, "Open for the owner").
