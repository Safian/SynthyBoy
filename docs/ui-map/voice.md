# Voice UI Map

## Purpose
The Voice page is the MPC overlay skin page for the "Voice" tab (`shell-mpc/overlay/overlay_layout.json:639-648`, `scripts/overlay-skin/mpc-ui-pages-overlay.json:163-169`).
It is implemented by `VoiceUi` (`shell-mpc/overlay/voice_ui.h:34-453`) using `VoiceScreen` (`shell-mpc/ui/voice_screen.h:26-297`) and `VoiceGrid` (`shell-mpc/ui/voice_grid.h:20-330`).
The view occupies the full tab area (1280 x 628) and provides per-voice sequence control over 8 steps and 6 modulation rows (PAN, DETUNE, CUTOFF, ENV, MOD 1, MOD 2) mapped to `voicectl.s1.pan` through `voicectl.s8.mod2` and `voicectl.length`. It also controls voice-wide random variation spreads (`voicectl.rand.pan`, `voicectl.rand.detune`, `voicectl.rand.cutoff`, `voicectl.rand.env`, `voicectl.rand.detune10x`), envelope and LFO time scaling (`voicectl.scale.env`, `voicectl.scale.lfo`, `voicectl.scale.lfo_mode`), and modulation destination routing switches (`voicectl.osc.a`, `voicectl.osc.b`, `voicectl.osc.c`, `voicectl.osc.sub`, `voicectl.osc.noise`).

## Layout
The Voice page occupies the entire tab region `(0, 110, 1280, 628)` (`shell-mpc/overlay/voice_ui.h:77-78`, `shell-mpc/ui/voice_screen.h:98`).
The layout consists of four main horizontal sections from top to bottom:

1. **Header area** (`rect.y + 4` to `rect.y + 76`):
   - Title: "VOICE CONTROL" at `(rect.x + 14, rect.y + 4)` (`shell-mpc/ui/voice_screen.h:244`).
   - Step Selector `qStepSelect`: Stepper displaying "STEP 1".."STEP 8" (width 140, height 56) at `(rect.x + 724, rect.y + 20)` (`shell-mpc/ui/voice_screen.h:190`).
   - Sequence Length `length`: NumberField displaying length 1..8 (width 180, height 56) at `(rect.x + 872, rect.y + 20)` (`shell-mpc/ui/voice_screen.h:189`).
   - Step Value `stepValue`: NumberField displaying the value of the active row at the selected step (width 200, height 56) at `(rect.x + 1060, rect.y + 20)` (`shell-mpc/ui/voice_screen.h:188`).

2. **Step Grid and Row Selector** (`rect.y + 84` to `rect.y + 320`, height 236):
   - Row List `rowBlockRect()`: Dedicated block at `(rect.x + 8, rect.y + 84, 252, 236)` containing `rowList` (`shell-mpc/ui/voice_screen.h:166, 192-193`). Displays 6 options (PAN, DETUNE, CUTOFF, ENV, MOD 1, MOD 2) in two columns of three rows (each 56 px high) with the title "ROW".
   - Step Grid `grid`: Sequence grid at `(rect.x + 268, rect.y + 84, 1004, 236)` (`shell-mpc/ui/voice_screen.h:194`, `shell-mpc/ui/voice_grid.h:48-64`). Contains 8 step columns with bipolar vertical bars (-100% to +100%) for the currently active row, zero baselines, handle discs, step numbers 1..8, and formatted value readouts beneath each column.

3. **Step Readout Strip** (`rect.y + 326` to `rect.y + 366`, height 40):
   - Strip `stripRect()` at `(rect.x + 8, rect.y + 326, 1264, 40)` (`shell-mpc/ui/voice_screen.h:167, 251-264`). Displays "STEP <selectedStep + 1>" on the left and all 6 row values (PAN, DETUNE, CUTOFF, ENV, MOD 1, MOD 2) for the currently selected step. The active row is drawn highlighted, while inactive rows are dimmed.

4. **Bottom Parameter Blocks** (`rect.y + 374` to `rect.y + 620`, height 246):
   - Block 0: "RANDOM" at `(rect.x + 8, rect.y + 374, 520, 246)` (`shell-mpc/ui/voice_screen.h:172, 197-208`). Contains four large knobs across the top (`randPan`, `randDetune`, `randCutoff`, `randEnv`) and the LED toggle `randDetune10x` ("10x DETUNE") at the bottom.
   - Block 1: "SCALING" at `(rect.x + 536, rect.y + 374, 380, 246)` (`shell-mpc/ui/voice_screen.h:173, 209-219`). Contains two medium knobs (`scaleEnv` "ENV TIME", `scaleLfo` "LFO TIME") and the LED list `scaleLfoMode` ("LFO TIME MODE": PERCENT / RATE) at the bottom.
   - Block 2: "TARGETS" at `(rect.x + 924, rect.y + 374, 348, 246)` (`shell-mpc/ui/voice_screen.h:174, 220-226`). Contains five LED toggles in a two-column grid (`oscA` "OSC A", `oscB` "OSC B", `oscC` "OSC C", `oscSub` "SUB", `oscNoise` "NOISE").

## Controls
| Label on screen | Parameter key | Gesture | Effect | Source |
|---|---|---|---|---|
| STEP | - | Tap arrow / drag / wheel / Q1 turn | Selects the active step (1..8) to display in the strip and edit via VALUE or Q4 | `shell-mpc/overlay/voice_ui.h:239` |
| LENGTH | `voicectl.length` | Vertical drag / wheel / Q3 turn | Sets the sequence length (1..8 steps) | `shell-mpc/overlay/voice_ui.h:238` |
| VALUE | - | Vertical drag / wheel / Q4 turn | Sets the value of the active row at the selected step (`voicectl.s1.pan`..`voicectl.s8.mod2`) | `shell-mpc/overlay/voice_ui.h:241` |
| ROW | - | Tap item / wheel / Q2 turn | Selects the active modulation row (PAN, DETUNE, CUTOFF, ENV, MOD 1, MOD 2) | `shell-mpc/overlay/voice_ui.h:240` |
| Step 1 Pan | `voicectl.s1.pan` | Touch drag / horizontal swipe | Sets step 1 pan offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 1 Detune | `voicectl.s1.detune` | Touch drag / horizontal swipe | Sets step 1 detune offset (-100 to +100 cents) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 1 Cutoff | `voicectl.s1.cutoff` | Touch drag / horizontal swipe | Sets step 1 filter cutoff offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 1 Env | `voicectl.s1.env` | Touch drag / horizontal swipe | Sets step 1 envelope time offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 1 Mod 1 | `voicectl.s1.mod1` | Touch drag / horizontal swipe | Sets step 1 mod 1 offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 1 Mod 2 | `voicectl.s1.mod2` | Touch drag / horizontal swipe | Sets step 1 mod 2 offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 2 Pan | `voicectl.s2.pan` | Touch drag / horizontal swipe | Sets step 2 pan offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 2 Detune | `voicectl.s2.detune` | Touch drag / horizontal swipe | Sets step 2 detune offset (-100 to +100 cents) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 2 Cutoff | `voicectl.s2.cutoff` | Touch drag / horizontal swipe | Sets step 2 filter cutoff offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 2 Env | `voicectl.s2.env` | Touch drag / horizontal swipe | Sets step 2 envelope time offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 2 Mod 1 | `voicectl.s2.mod1` | Touch drag / horizontal swipe | Sets step 2 mod 1 offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 2 Mod 2 | `voicectl.s2.mod2` | Touch drag / horizontal swipe | Sets step 2 mod 2 offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 3 Pan | `voicectl.s3.pan` | Touch drag / horizontal swipe | Sets step 3 pan offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 3 Detune | `voicectl.s3.detune` | Touch drag / horizontal swipe | Sets step 3 detune offset (-100 to +100 cents) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 3 Cutoff | `voicectl.s3.cutoff` | Touch drag / horizontal swipe | Sets step 3 filter cutoff offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 3 Env | `voicectl.s3.env` | Touch drag / horizontal swipe | Sets step 3 envelope time offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 3 Mod 1 | `voicectl.s3.mod1` | Touch drag / horizontal swipe | Sets step 3 mod 1 offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 3 Mod 2 | `voicectl.s3.mod2` | Touch drag / horizontal swipe | Sets step 3 mod 2 offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 4 Pan | `voicectl.s4.pan` | Touch drag / horizontal swipe | Sets step 4 pan offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 4 Detune | `voicectl.s4.detune` | Touch drag / horizontal swipe | Sets step 4 detune offset (-100 to +100 cents) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 4 Cutoff | `voicectl.s4.cutoff` | Touch drag / horizontal swipe | Sets step 4 filter cutoff offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 4 Env | `voicectl.s4.env` | Touch drag / horizontal swipe | Sets step 4 envelope time offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 4 Mod 1 | `voicectl.s4.mod1` | Touch drag / horizontal swipe | Sets step 4 mod 1 offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 4 Mod 2 | `voicectl.s4.mod2` | Touch drag / horizontal swipe | Sets step 4 mod 2 offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 5 Pan | `voicectl.s5.pan` | Touch drag / horizontal swipe | Sets step 5 pan offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 5 Detune | `voicectl.s5.detune` | Touch drag / horizontal swipe | Sets step 5 detune offset (-100 to +100 cents) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 5 Cutoff | `voicectl.s5.cutoff` | Touch drag / horizontal swipe | Sets step 5 filter cutoff offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 5 Env | `voicectl.s5.env` | Touch drag / horizontal swipe | Sets step 5 envelope time offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 5 Mod 1 | `voicectl.s5.mod1` | Touch drag / horizontal swipe | Sets step 5 mod 1 offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 5 Mod 2 | `voicectl.s5.mod2` | Touch drag / horizontal swipe | Sets step 5 mod 2 offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 6 Pan | `voicectl.s6.pan` | Touch drag / horizontal swipe | Sets step 6 pan offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 6 Detune | `voicectl.s6.detune` | Touch drag / horizontal swipe | Sets step 6 detune offset (-100 to +100 cents) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 6 Cutoff | `voicectl.s6.cutoff` | Touch drag / horizontal swipe | Sets step 6 filter cutoff offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 6 Env | `voicectl.s6.env` | Touch drag / horizontal swipe | Sets step 6 envelope time offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 6 Mod 1 | `voicectl.s6.mod1` | Touch drag / horizontal swipe | Sets step 6 mod 1 offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 6 Mod 2 | `voicectl.s6.mod2` | Touch drag / horizontal swipe | Sets step 6 mod 2 offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 7 Pan | `voicectl.s7.pan` | Touch drag / horizontal swipe | Sets step 7 pan offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 7 Detune | `voicectl.s7.detune` | Touch drag / horizontal swipe | Sets step 7 detune offset (-100 to +100 cents) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 7 Cutoff | `voicectl.s7.cutoff` | Touch drag / horizontal swipe | Sets step 7 filter cutoff offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 7 Env | `voicectl.s7.env` | Touch drag / horizontal swipe | Sets step 7 envelope time offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 7 Mod 1 | `voicectl.s7.mod1` | Touch drag / horizontal swipe | Sets step 7 mod 1 offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 7 Mod 2 | `voicectl.s7.mod2` | Touch drag / horizontal swipe | Sets step 7 mod 2 offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 8 Pan | `voicectl.s8.pan` | Touch drag / horizontal swipe | Sets step 8 pan offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 8 Detune | `voicectl.s8.detune` | Touch drag / horizontal swipe | Sets step 8 detune offset (-100 to +100 cents) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 8 Cutoff | `voicectl.s8.cutoff` | Touch drag / horizontal swipe | Sets step 8 filter cutoff offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 8 Env | `voicectl.s8.env` | Touch drag / horizontal swipe | Sets step 8 envelope time offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 8 Mod 1 | `voicectl.s8.mod1` | Touch drag / horizontal swipe | Sets step 8 mod 1 offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| Step 8 Mod 2 | `voicectl.s8.mod2` | Touch drag / horizontal swipe | Sets step 8 mod 2 offset (-100% to +100%) | `shell-mpc/overlay/voice_ui.h:247` |
| PAN | `voicectl.rand.pan` | Rotary drag / wheel / Q1 turn | Sets random pan spread (0% to 100%) | `shell-mpc/overlay/voice_ui.h:249` |
| DETUNE | `voicectl.rand.detune` | Rotary drag / wheel / Q2 turn | Sets random detune spread (0% to 100%) | `shell-mpc/overlay/voice_ui.h:250` |
| CUTOFF | `voicectl.rand.cutoff` | Rotary drag / wheel / Q3 turn | Sets random filter cutoff spread (0% to 100%) | `shell-mpc/overlay/voice_ui.h:251` |
| ENV | `voicectl.rand.env` | Rotary drag / wheel / Q4 turn | Sets random envelope time spread (0% to 100%) | `shell-mpc/overlay/voice_ui.h:252` |
| 10x DETUNE | `voicectl.rand.detune10x` | Tap toggle / Q1 turn | Multiplies random detune amount by 10 (Off / On) | `shell-mpc/overlay/voice_ui.h:253` |
| ENV TIME | `voicectl.scale.env` | Rotary drag / wheel / Q1 turn | Scales envelope times across voices (25% to 400%) | `shell-mpc/overlay/voice_ui.h:254` |
| LFO TIME | `voicectl.scale.lfo` | Rotary drag / wheel / Q2 turn | Scales LFO times across voices (25% to 400%) | `shell-mpc/overlay/voice_ui.h:255` |
| LFO TIME MODE | `voicectl.scale.lfo_mode` | Tap option / Q3 turn | Sets LFO scaling mode (Percent / Rate) | `shell-mpc/overlay/voice_ui.h:256` |
| OSC A | `voicectl.osc.a` | Tap toggle / Q1 turn | Enables voice control modulation for Oscillator A (Off / On) | `shell-mpc/overlay/voice_ui.h:257` |
| OSC B | `voicectl.osc.b` | Tap toggle / Q2 turn | Enables voice control modulation for Oscillator B (Off / On) | `shell-mpc/overlay/voice_ui.h:258` |
| OSC C | `voicectl.osc.c` | Tap toggle / Q3 turn | Enables voice control modulation for Oscillator C (Off / On) | `shell-mpc/overlay/voice_ui.h:259` |
| SUB | `voicectl.osc.sub` | Tap toggle / Q4 turn | Enables voice control modulation for Sub oscillator (Off / On) | `shell-mpc/overlay/voice_ui.h:260` |
| NOISE | `voicectl.osc.noise` | Tap toggle / Q1 turn | Enables voice control modulation for Noise generator (Off / On) | `shell-mpc/overlay/voice_ui.h:261` |

## Opens
The Voice page does not open any popup, detail page, or full screen during normal operation.
- The class defines a list popup `list_` (`ui::ListPopup`, `kPageList = 15`) at `shell-mpc/overlay/voice_ui.h:79-82, 443`, renders it at line 203, and handles backdrop dismiss and item selection at lines 324-335.
- However, the trigger to open `list_` (`shell-mpc/overlay/voice_ui.h:356-370`) requires a control with `T::Stepper` emitting `ChangeKind::Request`. In `VoiceUi::configure` (`shell-mpc/overlay/voice_ui.h:236-262`), no control is bound as `T::Stepper` with a parameter (`qStepSelect` is bound as `T::Inert`, `scaleLfoMode` is a `T::List` `LedList`). Thus, `list_` is unreachable.
- No modulation details page or keyboard entry page is wired (`ChangeKind::ModsRequest` is unhandled in `VoiceUi::handle` at `shell-mpc/overlay/voice_ui.h:320-391`).

## Q-Link groups
The page defines 6 Q-Link groups (`VoiceScreen::kMaxGroups = 6`, `shell-mpc/ui/voice_screen.h:29`, `shell-mpc/overlay/voice_ui.h:400-435`):

1. **Group 0: "STEP"** (`shell-mpc/overlay/voice_ui.h:407-413`, `shell-mpc/ui/voice_screen.h:271-276`):
   - Q1: Virtual step target (`VoiceUi::kVirtualStep = 0`), sets `full_.grid.state.selectedStep` (0..7, step 1..8).
   - Q2: Virtual row target (`VoiceUi::kVirtualRow = 1`), sets `full_.grid.state.activeRow` (0..5: PAN, DETUNE, CUTOFF, ENV, MOD 1, MOD 2).
   - Q3: `voicectl.length` (sequence length, 1..8).
   - Q4: Dynamic step value `stepParam(activeRow, selectedStep)` (`voicectl.s1.pan`..`voicectl.s8.mod2`).

2. **Group 1: "RANDOM"** (`shell-mpc/overlay/voice_ui.h:414-418`, `shell-mpc/ui/voice_screen.h:277-280`):
   - Q1: `voicectl.rand.pan`
   - Q2: `voicectl.rand.detune`
   - Q3: `voicectl.rand.cutoff`
   - Q4: `voicectl.rand.env`

3. **Group 2: "RAND 10x"** (`shell-mpc/overlay/voice_ui.h:419-420`, `shell-mpc/ui/voice_screen.h:282`):
   - Q1: `voicectl.rand.detune10x`
   - Q2..Q4: unassigned

4. **Group 3: "SCALING"** (`shell-mpc/overlay/voice_ui.h:421-424`, `shell-mpc/ui/voice_screen.h:284-287`):
   - Q1: `voicectl.scale.env`
   - Q2: `voicectl.scale.lfo`
   - Q3: `voicectl.scale.lfo_mode`
   - Q4: unassigned

5. **Group 4: "TARGETS"** (`shell-mpc/overlay/voice_ui.h:425-429`, `shell-mpc/ui/voice_screen.h:288-292`):
   - Q1: `voicectl.osc.a`
   - Q2: `voicectl.osc.b`
   - Q3: `voicectl.osc.c`
   - Q4: `voicectl.osc.sub`

6. **Group 5: "NOISE"** (`shell-mpc/overlay/voice_ui.h:430-432`, `shell-mpc/ui/voice_screen.h:294`):
   - Q1: `voicectl.osc.noise`
   - Q2..Q4: unassigned

Group 0 is activated by tapping inside the step grid, row selector, strip, or header controls (`shell-mpc/ui/voice_screen.h:120-123`). Groups 1 through 5 are activated by touching controls within their respective blocks (`shell-mpc/ui/voice_screen.h:126-152`). The active group is published to hardware encoders Q1..Q4, while the next three groups wrap around to Q5..Q16 (`shell-mpc/overlay/voice_ui.h:405, 434`).

## Live parts
1. **Sounding Voice Step Highlighting**:
   - In `VoiceUi::update` (`shell-mpc/overlay/voice_ui.h:99-107`), active voice steps are sampled from `in.live->voiceStep[v]` for the newest voices (`synthyboy::LiveValues::kReadVoices`). If a voice plays a valid step within `voicectl.length`, bit `s` is set in `voiceStepMask`.
   - In `VoiceGrid::drawBody` (`shell-mpc/ui/voice_grid.h:188-207`), any step with an active voice receives a signal flow color background tint (`T.flow[0]`, alpha 34), a bright 2.2 px border in `T.flow[0]`, and a 4.5 px circular indicator dot in the upper right corner of the step column header.

2. **Selected Step and Row Highlight**:
   - The selected step column is outlined with a 1.8 px border in Q-Link accent color `T.q` (`shell-mpc/ui/voice_grid.h:194-196`).
   - The readout strip below the grid (`shell-mpc/ui/voice_screen.h:252-263`) updates dynamically to show `STEP <selectedStep + 1>` and every row's value for the selected step; the active row is highlighted in bright text (`T.text`), while inactive rows are dimmed (`T.textDim`).
   - The `stepValue` NumberField in the header (`shell-mpc/overlay/voice_ui.h:116-121`) updates its label to match the active row name and displays the formatted value.

3. **Step Length Masking**:
   - Steps beyond the active sequence length (`s >= voicectl.length`) are visually dimmed (`T.bg1` with alpha 120, border `T.track`, header text `T.textDim`) and cannot display sounding voice indicator dots (`shell-mpc/ui/voice_grid.h:186-202`).

## Duplicates
None. None of the parameter keys bound on the Voice page (`voicectl.length`, `voicectl.s1.pan`..`voicectl.s8.mod2`, `voicectl.rand.pan`, `voicectl.rand.detune`, `voicectl.rand.cutoff`, `voicectl.rand.env`, `voicectl.rand.detune10x`, `voicectl.scale.env`, `voicectl.scale.lfo`, `voicectl.scale.lfo_mode`, `voicectl.osc.a`, `voicectl.osc.b`, `voicectl.osc.c`, `voicectl.osc.sub`, `voicectl.osc.noise`) appear on any other overlay page or tab under `shell-mpc/`.
Note that `voice.phase_legato` shares a `voice.` prefix but is bound exclusively on the Master page (`shell-mpc/overlay/master_ui.h:252`) and does not appear on the Voice page.

## Questionable
1. **NumberField length writes 0-based step index instead of 1-based value (Bug)**:
   - In `shell-mpc/overlay/voice_ui.h:386`, `handle()` processes `ChangeKind::Index` with `uibind::writePlain (inst, it->param, ch.index);`.
   - `NumberField` emits `Change::indexed (id_, idx, state.min + idx * stepSize())` (`shell-mpc/ui/base.h:621`). For `voicectl.length` (min 1, max 8, step 1), the field index member (0..7) is passed as ch.index, while the actual length value (1..8) is stored in ch.value. As a result, dragging `length` writes 0..7 to `voicectl.length` instead of 1..8. In contrast, `stepValue` explicitly writes ch.value at `shell-mpc/overlay/voice_ui.h:350`.

2. **Unreachable `ListPopup list_` and dead stepper request handler**:
   - `VoiceUi` instantiates `ui::ListPopup list_` (`shell-mpc/overlay/voice_ui.h:443`), positions it in `setCanvas` (`shell-mpc/overlay/voice_ui.h:79-82`), renders it (`shell-mpc/overlay/voice_ui.h:203-204`), and handles backdrop/selection events (`shell-mpc/overlay/voice_ui.h:324-335`).
   - The opening condition `it->type == T::Stepper && ch.kind == ui::ChangeKind::Request` (`shell-mpc/overlay/voice_ui.h:356-370`) can never be satisfied because no parameter item is bound as `T::Stepper` in `VoiceUi::configure` (`shell-mpc/overlay/voice_ui.h:236-262`) (`qStepSelect` is bound as `T::Inert`, `scaleLfoMode` is a `T::List`). The popup is dead code.

3. **No modulation routing support on knobs**:
   - Unlike other overlay pages (Filter, Osc, Master) where knobs respond to a 600 ms long press by emitting `ChangeKind::ModsRequest` to open modulation assignment, `VoiceUi::handle` (`shell-mpc/overlay/voice_ui.h:320-391`) has no handler for `ChangeKind::ModsRequest`. Long presses on the random and scaling knobs do nothing.

4. **Single-control Q-Link groups wasting encoder banks**:
   - Group 2 ("RAND 10x", `shell-mpc/overlay/voice_ui.h:420`) holds only `voicectl.rand.detune10x`, leaving Q2..Q4 empty.
   - Group 5 ("NOISE", `shell-mpc/overlay/voice_ui.h:431`) holds only `voicectl.osc.noise`, leaving Q2..Q4 empty.
   - These groups exist because `RANDOM` has 4 knobs + 1 toggle, and `TARGETS` has 5 toggles, both exceeding the 4-slot capacity of a single QGroup by 1 control. Allocating an entire Q-Link hardware bank of 4 encoders to a single toggle is inefficient.

5. **`voice.phase_legato` placed on Master page rather than Voice page**:
   - Parameter `voice.phase_legato` (`docs/params.json`) is titled "Voice Phase Legato", but is displayed on the Master page (`shell-mpc/overlay/master_ui.h:252`) and absent from the Voice page.

6. **Redundant step selection and value adjustment controls**:
   - Step selection can be adjusted via three separate mechanisms: tapping columns in `grid` (`shell-mpc/ui/voice_grid.h:98`), turning encoder Q1 (`shell-mpc/overlay/voice_ui.h:409`), or tapping arrows on `qStepSelect` (`shell-mpc/overlay/voice_ui.h:336`).
   - Step value can be adjusted via three separate mechanisms: dragging bars in `grid` (`shell-mpc/ui/voice_grid.h:96`), dragging the `stepValue` NumberField in the header (`shell-mpc/overlay/voice_ui.h:346`), or turning encoder Q4 (`shell-mpc/overlay/voice_ui.h:412`).
   - The strip below the grid (`shell-mpc/ui/voice_screen.h:252-263`) displays all 6 row values for the active step, but is inert and cannot be interacted with to edit values.
