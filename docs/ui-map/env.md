# UI Map: Envelope Page (`envui`)
Refreshed 2026-11-23 against the three full-screen Q groups and the compact Play controls.

> **Update 2026-11-26 (one tab page = one screen).** The Env key has SIX sub-tabs again: `Env`, `Env 2` .. `Env 6` (views `env1` .. `env6` of `shell-mpc/overlay/overlay_layout.json`, the view's `index` is the envelope). The page has NO selector: `EnvUi::update` takes the envelope from `page::Input::instance` (`shownEnv`), nothing of it is shared with the Play page (`Play Env` is neither read nor written by the tab, and leaving the tab does not fall back to another envelope); the Q-Link virtual selector of the SHAPE block and the native selector cells of the tab are gone, the top bar shows the caption `ENV n` where the selector was and the tags take its width. The Play page keeps its carousel and the expanded editor its selector strip (`Play Expanded Envelope Selector`). The text below that speaks of the tab's selector describes the Play expansion.
>
> **Update 2026-11-21 (bold cut, step 2; `docs/ui-map/env-lfo-bold-cut/README.md`).** ONE tab `Env`. The selector is the used-item selector ("used + one free": ENV 1 always, the used envelopes, the selected one, one `+`), the selection is `Play Env` (shared with the Play page, chunk saved), choosing writes nothing, the Q-Links and the graph follow the selected envelope. The LEGATO switch is labelled RETRIG now (Questionable 2 below: ON restarts the envelope on a legato note) and the dead `T::Field` path (Questionable 1) is deleted. 13 controls, 3 Q groups. The text below describes the page before the merge where it names the six fixed cells or the four tabs.

## Purpose

The Envelope page provides comprehensive configuration and real-time visualization of the DAHDSR modulation envelopes (Delay, Attack, Hold, Decay, Sustain, Release, and curvature) in SynthyBoy.
In the overlay layout (`shell-mpc/overlay/overlay_layout.json:268-390`), the page kind `envui` defines SIX tabs since 2026-11-26: `env1` .. `env6` (tabs "Env", "Env 2" .. "Env 6", the sub-tabs of the function key Env); the hardware sub-tab chooses the envelope.
Each tab instantiates `PageView<EnvUi>` via `pages::make()` (`shell-mpc/overlay/pages.h:35, 65`), configuring `EnvUi` in full-screen tab mode (`shell-mpc/overlay/env_ui.h:127-136`).

On the standalone tab the host owns the eleven scalar cells and TARGETS. The active envelope and MS/BEATS choice determine the scalar bindings. Double tap a dial or press Enter while it has focus to open the MPC numeric editor. A stationary long press opens the overlay modulation detail for that dial. TARGETS shows the currently selected envelope’s routed destination count and opens the overlay’s grouped routing editor. Both overlay details hide the native cells while open. The curve and its running dots stay with the overlay.

The same underlying `EnvUi` implementation powers the compact envelope panel occupying the middle third of the Play tab (`shell-mpc/overlay/play_view.h:490-496`, `shell-mpc/overlay/env_ui.h:73, 82`), whose expand button opens the same full-screen view (`shell-mpc/overlay/env_ui.h:344, 850`).

The page supports 6 identical DAHDSR envelope generators (`env1` through `env6`; `shell-mpc/overlay/env_ui.h:303`, `shell-mpc/ui/env_screen.h:19`):
1. Multi-stage timing: Delay (0..4000 ms or 0..8 beats), Attack (0..10000 ms or 0..16 beats), Hold (0..4000 ms or 0..8 beats), Decay (0..10000 ms or 0..16 beats), Sustain level (0..100%), and Release (0..10000 ms or 0..16 beats).
2. Continuous curve shaping: Attack, Decay, and Release segments each feature an independent curvature parameter (-20..+20: logarithmic/fast-start, linear, or exponential/slow-start).
3. Time units: Independent millisecond or tempo-synced beat division units (`units`).
4. Retriggering: Legato mode toggle (`legato_inverted`).
5. Touch interaction: Interactive graph with draggable stage boundary handles, 2D decay/sustain handle, vertical curve bend handles, double-tap reset to default, and zoomable time axis ruler.
6. Reference ghost: Background rendering of Envelope 1 (the amp envelope) when viewing Envelopes 2 through 6.
7. Destination routing: Integrated access to the modulation matrix for envelope routing (`ModTargetsPage`) and per-parameter modulation depth inspection (`ModulationsPage`).

## Layout

The overlay operates on a 1280 x 800 display, occupying the primary tab area from y = 110 to y = 738 (width 1280, height 628; `shell-mpc/overlay/overlay_layout.json:273-278`, `shell-mpc/overlay/env_ui.h:72, 81-83`).

```
+----------------------------------------------------------------------------------------------------+ y=110
| ENV n   Destination tags                                                     [TARGETS 2] | Top bar (page mode; CLOSE only in the full screen over the Play page)
+----------------------------------------------------------------------------------------------------+ y=182
|                                                                                           [LOCKED] |
|                                                                                                    |
|                                    DAHDSR ENVELOPE GRAPH                                           | Graph
|                                 (interactive curve & handles)                                      |
|                                                                                                    |
|  0 ms                 200 ms                 400 ms                 600 ms                  800 ms | Time ruler
+------------------------------------+-------------------------------+-------------------------------+ y=616
| ADSR                               | SHAPE                         | TIME                          |
|  [ATTACK] [DECAY] [SUSTAIN][RELEASE|  [A CURVE] [D CURVE] [R CURVE]|  [DELAY] [HOLD] [BEATS][LEGATO| Bottom band
+------------------------------------+-------------------------------+-------------------------------+ y=792
```

### 1. Top Bar (y = 110 to 182, h = 72; `shell-mpc/ui/env_screen.h:167, 253-261`)
- Left: `CLOSE` button (width 96 px at x = 8; hidden when running as a dedicated tab page via `pageMode_`, visible when opened as full-screen modal from Play page; `shell-mpc/ui/env_screen.h:215, 255-257`).
- Center-Left: `EnvSelector` (width up to 720 px, 6 cells x 120 px; `shell-mpc/ui/env_screen.h:28-73, 258-259`). Shows "ENV 1" through "ENV 6", with the active envelope outlined in its theme color and envelopes with active modulation targets marked with an indicator dot.
- Center-Right: Target readout tags (`tags_`, x = selector right + 16 to targets button - 12; `shell-mpc/ui/env_screen.h:261, 303-333`). Displays up to 3 modulation destinations with amounts (e.g. `CUTOFF  +35%`, `+1`), rendered as text with colored discs.
- Right: `TARGETS` button (width 200 px at x = 1072, h = 56; `shell-mpc/ui/env_screen.h:170, 260`). Displays the count of active modulation destinations driven by this envelope and opens `ModTargetsPage`.

### 2. Middle Section: Envelope Graph (y = 182 to 608, h = 426; `shell-mpc/ui/env_screen.h:290-292`, `shell-mpc/ui/env_graph.h:107-523`)
- Bounded at x = 8, y = 190, w = 1264, h = 418.
- Plot area: x = 32, y = 228, w = 1216, h = 340 (`shell-mpc/ui/env_graph.h:144`).
- Top-Right: `LOCK` button (the right edge of the frame minus 6 px, 76 x 34, in the margin above the plot; it sat at the top left until 2026-11-26, where it touched the first handle of the envelope; `lockRect` in `shell-mpc/ui/env_graph.h`). Toggles between `LOCKED` (envelope continuously auto-fits available width) and `FREE` (unlocked zoom).
- Canvas elements:
  * Horizontal grid lines at 0%, 50%, and 100% sustain levels (`shell-mpc/ui/env_graph.h:400-405`).
  * Time ruler along the bottom with tick marks and round labels in milliseconds/seconds or beats (`shell-mpc/ui/env_graph.h:408-431`). In `FREE` mode, dragging along this ruler zooms the time scale (`shell-mpc/ui/env_graph.h:562-577`).
  * Ghost curve: Faint rendering of Envelope 1 behind Envelopes 2..6 (`shell-mpc/ui/env_graph.h:452-461`).
  * Active envelope curve: Gradient fill and crisp solid polyline with power curves (`shell-mpc/ui/env_graph.h:464-477`).
  * 8 touch handles (`shell-mpc/ui/env_graph.h:480-503`): Delay end, Attack end, Hold end, Decay/Sustain 2D handle, Release end, and 3 curvature bend handles (Attack Curve, Decay Curve, Release Curve).
  * Value bubble popup over the active/dragged handle showing exact value and unit (`shell-mpc/ui/env_graph.h:505-522`).
  * Running voice dots: Animated indicators tracking voice envelope positions (`shell-mpc/ui/env_graph.h:247-273`).

### 3. Bottom Band (y = 616 to 792, h = 176; `shell-mpc/ui/env_screen.h:167, 263-288`)
Organized into 3 titled blocks corresponding to Q-Link banks (proportional width weights 4:3:4; `shell-mpc/ui/env_screen.h:265`):
- Block 0 ("ADSR", width ~452 px; `shell-mpc/ui/env_screen.h:265, 283`):
  Four knobs side-by-side: `ATTACK`, `DECAY`, `SUSTAIN`, `RELEASE`.
- Block 1 ("SHAPE", width ~339 px; `shell-mpc/ui/env_screen.h:265, 284`):
  Three knobs side-by-side: `A CURVE`, `D CURVE`, `R CURVE` (bipolar, -20..+20).
- Block 2 ("TIME", width ~457 px; `shell-mpc/ui/env_screen.h:265, 285`):
  Two knobs: `DELAY`, `HOLD`; and two LED toggle buttons: `BEATS` (switches between ms and tempo-synced beat units) and `LEGATO` (retriggering behavior).

### 4. Compact View (Play Page Middle Third: x = 440, y = 114, w = 400, h = 620; `shell-mpc/overlay/env_ui.h:73, 82`, `shell-mpc/ui/env_screen.h:102-160`)
- Top: `EnvCarousel` selector (x = 446, y = 120, w = 388, h = 56; `shell-mpc/ui/env_screen.h:147`). Steps through ENV 1..6; tapping name opens vertical list popup.
- Middle: Compact `EnvGraph` (x = 446, y = 184, w = 388, h = 166; `shell-mpc/ui/env_screen.h:149-151`) with expand button `full` at top right (x = 794, y = 188, w = 36, h = 36; `shell-mpc/ui/env_screen.h:152`). Graph is permanently locked (no lock toggle or ruler zoom).
- Bottom: Four knobs (`ATTACK`, `DECAY`, `SUSTAIN`, `RELEASE`) arranged in a 2x2 grid with side dials (`shell-mpc/ui/env_screen.h:153-159`).

## Controls

| Label on screen | Parameter key | Gesture | Effect | Source |
| --- | --- | --- | --- | --- |
| CLOSE | - | Tap | Closes full-screen envelope page and returns to previous stack page (`stack.back()`; hidden in dedicated tab page mode) | `shell-mpc/overlay/env_ui.h:343` |
| ENV selector | - | Tap cell | Selects the active envelope generator | `shell-mpc/overlay/env_ui.h:347` |
| TARGETS | - | Tap | Opens `ModTargetsPage` showing modulation matrix destinations routed from this envelope | `shell-mpc/overlay/env_ui.h:345` |
| LOCKED / FREE | - | Tap | Toggles time axis scaling between LOCKED (curve auto-fits width) and FREE (manual ruler zoom) | `shell-mpc/ui/env_graph.h:147` |
| Ruler zoom band | - | Horizontal drag | Zooms time scale in or out (one e-fold per 160 px; FREE mode only) | `shell-mpc/ui/env_graph.h:148` |
| Delay handle | `env1.delay` | Horizontal drag / Double-tap | Adjusts delay time before attack begins (0..4000 ms or `env1.delay_beats` 0..8 beats); double-tap resets to default | `shell-mpc/overlay/env_ui.h:1015` |
| Attack handle | `env1.attack` | Horizontal drag / Double-tap | Adjusts attack time to peak (0..10000 ms or `env1.attack_beats` 0..16 beats); double-tap resets to default | `shell-mpc/overlay/env_ui.h:1015` |
| Hold handle | `env1.hold` | Horizontal drag / Double-tap | Adjusts hold time at peak level (0..4000 ms or `env1.hold_beats` 0..8 beats); double-tap resets to default | `shell-mpc/overlay/env_ui.h:1015` |
| Decay / Sustain handle | `env1.decay` | Horizontal drag (time) / Vertical drag (sustain) / Double-tap | 2D handle: horizontal drag adjusts decay time (0..10000 ms or `env1.decay_beats` 0..16 beats); vertical drag adjusts sustain level (`env1.sustain` 0..100%); double-tap resets to default | `shell-mpc/overlay/env_ui.h:1015` |
| Release handle | `env1.release` | Horizontal drag / Double-tap | Adjusts release time (0..10000 ms or `env1.release_beats` 0..16 beats); double-tap resets to default | `shell-mpc/overlay/env_ui.h:1015` |
| Attack Curve handle | `env1.attack_curve` | Vertical drag / Double-tap | Bends attack curvature (-20..+20: negative = logarithmic, positive = exponential); double-tap resets to default | `shell-mpc/overlay/env_ui.h:1024` |
| Decay Curve handle | `env1.decay_curve` | Vertical drag / Double-tap | Bends decay curvature (-20..+20); double-tap resets to default | `shell-mpc/overlay/env_ui.h:1024` |
| Release Curve handle | `env1.release_curve` | Vertical drag / Double-tap | Bends release curvature (-20..+20); double-tap resets to default | `shell-mpc/overlay/env_ui.h:1024` |
| ATTACK | `env1.attack` | Drag / Turn Q-Link / Wheel / Long-press | Sets attack time (0..10000 ms or `env1.attack_beats` 0..16 beats); long-press opens `ModulationsPage` | `shell-mpc/overlay/env_ui.h:357` |
| DECAY | `env1.decay` | Drag / Turn Q-Link / Wheel / Long-press | Sets decay time (0..10000 ms or `env1.decay_beats` 0..16 beats); long-press opens `ModulationsPage` | `shell-mpc/overlay/env_ui.h:357` |
| SUSTAIN | `env1.sustain` | Drag / Turn Q-Link / Wheel / Long-press | Sets sustain level held while key is depressed (0..100%); long-press opens `ModulationsPage` | `shell-mpc/overlay/env_ui.h:357` |
| RELEASE | `env1.release` | Drag / Turn Q-Link / Wheel / Long-press | Sets release time (0..10000 ms or `env1.release_beats` 0..16 beats); long-press opens `ModulationsPage` | `shell-mpc/overlay/env_ui.h:357` |
| A CURVE | `env1.attack_curve` | Drag / Turn Q-Link / Wheel / Long-press | Sets attack curve curvature (-20..+20, bipolar); long-press opens `ModulationsPage` | `shell-mpc/overlay/env_ui.h:357` |
| D CURVE | `env1.decay_curve` | Drag / Turn Q-Link / Wheel / Long-press | Sets decay curve curvature (-20..+20, bipolar); long-press opens `ModulationsPage` | `shell-mpc/overlay/env_ui.h:357` |
| R CURVE | `env1.release_curve` | Drag / Turn Q-Link / Wheel / Long-press | Sets release curve curvature (-20..+20, bipolar); long-press opens `ModulationsPage` | `shell-mpc/overlay/env_ui.h:357` |
| DELAY | `env1.delay` | Drag / Turn Q-Link / Wheel / Long-press | Sets pre-delay time before attack begins (0..4000 ms or `env1.delay_beats` 0..8 beats); long-press opens `ModulationsPage` | `shell-mpc/overlay/env_ui.h:357` |
| HOLD | `env1.hold` | Drag / Turn Q-Link / Wheel / Long-press | Sets hold time at peak before decay begins (0..4000 ms or `env1.hold_beats` 0..8 beats); long-press opens `ModulationsPage` | `shell-mpc/overlay/env_ui.h:357` |
| BEATS | `env1.units` | Tap / Turn Q-Link | Toggles time units between milliseconds (0) and tempo-synced beats (1) | `shell-mpc/overlay/env_ui.h:361` |
| RETRIG | `env1.legato_inverted` | Tap / Turn Q-Link | Restarts the envelope on a legato note when enabled | `shell-mpc/overlay/env_ui.h:362` |
| ENV (Play selector) | - | Tap the native menu | Selects ENV 1–6 in compact Play | `scripts/mpc-skin-extras/82_play.py` |
| Full Screen (Expand icon) | - | Tap | Expands Play compact envelope panel into full-screen envelope page (`EnvFullScreen`) | `shell-mpc/overlay/env_ui.h:344` |
| ATTACK (Compact) | `env1.attack` | Drag / Turn Q-Link / Wheel / Long-press | Sets attack time in compact view (0..10000 ms or beats) | `shell-mpc/overlay/env_ui.h:359` |
| DECAY (Compact) | `env1.decay` | Drag / Turn Q-Link / Wheel / Long-press | Sets decay time in compact view (0..10000 ms or beats) | `shell-mpc/overlay/env_ui.h:359` |
| SUSTAIN (Compact) | `env1.sustain` | Drag / Turn Q-Link / Wheel / Long-press | Sets sustain level in compact view (0..100%) | `shell-mpc/overlay/env_ui.h:359` |
| RELEASE (Compact) | `env1.release` | Drag / Turn Q-Link / Wheel / Long-press | Sets release time in compact view (0..10000 ms or beats) | `shell-mpc/overlay/env_ui.h:359` |

## Opens

### 1. Modulation Targets Page (`ModTargetsPage`, `kPageTargets = 30`)
- Opened by: Tapping `TARGETS` button on the full-screen envelope page (`shell-mpc/overlay/env_ui.h:852`).
- Purpose: Lists all modulation matrix destination routings driven by the active envelope, allowing depth, polarity, bypass, and slot removal configuration.
- Controls on `ModTargetsPage`:

| Label on screen | Parameter key | Gesture | Effect | Source |
| --- | --- | --- | --- | --- |
| BACK | - | Tap | Returns to previous stack page (`stack.back()`) | `shell-mpc/overlay/env_ui.h:851` |
| PAGE | - | Tap left/right / Turn | Pages through target entries (up to 6 per page) | `shell-mpc/overlay/env_ui.h:917` |
| ADD TARGET | - | Tap | Opens destination picker list popup (`kPageList = 31`) | `shell-mpc/overlay/env_ui.h:853` |
| DEPTH (Row 1..6) | `mod01.amount` | Drag / Turn Q-Link | Sets modulation depth percentage (-100%..+100%, bipolar) | `shell-mpc/overlay/env_ui.h:939` |
| UNI / BI (Row 1..6) | `mod01.bipolar` | Tap | Toggles destination modulation polarity between unipolar and bipolar | `shell-mpc/overlay/env_ui.h:940` |
| ON / OFF (Row 1..6) | `mod01.bypass` | Tap | Toggles modulation slot bypass on or off | `shell-mpc/overlay/env_ui.h:941` |
| X (Row 1..6) | `mod01.source` | Tap | Clears modulation slot (resets source, destination, and amount) | `shell-mpc/overlay/env_ui.h:858` |

### 2. Destination Picker List Popup (`ListPopup`, `kPageList = 31`)
- Opened by: Tapping `ADD TARGET` on `ModTargetsPage` (`shell-mpc/overlay/env_ui.h:853`).
- Purpose: Hierarchical two-level list popup to browse and select destination parameters for a new modulation routing.
- Controls:
  * Category group list: Tap category row (Oscillators, Filters, LFOs, Envelopes, Effects, Master) to view destinations (`shell-mpc/overlay/env_ui.h:895-899`).
  * Destination item list: Tap parameter row to bind free modulation slot to `mod01.destination` with active envelope as source, setting default amount to 50% (`shell-mpc/overlay/env_ui.h:901-906, 1030-1047`).
  * Header back arrow: Tap header to navigate back from parameter list to category groups (`shell-mpc/overlay/env_ui.h:854`).
  * Backdrop: Tap outside popup to dismiss and return to `ModTargetsPage` (`shell-mpc/overlay/env_ui.h:851`).

### 3. Modulations Detail Page (`ModulationsPage`, `kPageMods = 32`)
- Opened by: Long-press gesture on any envelope knob on the full-screen or compact envelope view (`shell-mpc/overlay/env_ui.h:882-887`).
- Purpose: Inspects and manages all modulation matrix sources driving that specific parameter.
- Controls on `ModulationsPage`:

| Label on screen | Parameter key | Gesture | Effect | Source |
| --- | --- | --- | --- | --- |
| BACK | - | Tap | Returns to previous stack page (`stack.back()`) | `shell-mpc/overlay/env_ui.h:851` |
| Source Board (Chips) | `mod01.source` | Tap chip | When no modulations exist, tapping a source chip on the board immediately connects it to this parameter | `shell-mpc/overlay/env_ui.h:888` |
| ADD SOURCE | - | Tap left/right / Turn | Selects modulation source to add from available list | `shell-mpc/overlay/env_ui.h:923` |
| ADD | `mod01.destination` | Tap | Allocates free modulation slot connecting chosen source to this parameter | `shell-mpc/overlay/env_ui.h:1048` |
| DEPTH (Row 1..8) | `mod01.amount` | Drag / Turn Q-Link | Sets modulation depth for this incoming source | `shell-mpc/overlay/env_ui.h:956` |
| UNI / BI (Row 1..8) | `mod01.bipolar` | Tap | Toggles unipolar / bipolar mode for this incoming source | `shell-mpc/overlay/env_ui.h:957` |
| ON / OFF (Row 1..8) | `mod01.bypass` | Tap | Toggles bypass on or off for this incoming source | `shell-mpc/overlay/env_ui.h:958` |
| X (Row 1..8) | `mod01.source` | Tap | Removes incoming modulation slot | `shell-mpc/overlay/env_ui.h:870` |

### 4. Instance Selection Popup (`ListPopup`, `instPopup_`)
- Opened by: Tapping the envelope name on the `instances` Carousel in the compact view on the Play page (`shell-mpc/overlay/env_ui.h:849`).
- Purpose: Direct popup menu over the compact third to jump directly to any of the 6 envelopes.
- Controls:
  * Row 1..6 ("ENV 1" .. "ENV 6"): Tap row to select active envelope instance (`shell-mpc/overlay/env_ui.h:833-840`). Rows display indicator dots in the envelope's color if that envelope modulates active targets.
  * Backdrop: Tap outside menu to dismiss popup (`shell-mpc/overlay/env_ui.h:841-842`).

### 5. Full-Screen Envelope View (`EnvFullScreen`, `kElementEnv = 2`)
- Opened by: Tapping the expand button (`full`) in the header band of the compact view on the Play page (`shell-mpc/overlay/env_ui.h:850`).
- Dedicated tab mode: Also displayed directly when switching to tabs `Env 1` through `Env 4` (`shell-mpc/overlay/pages.h:65`).

## Q-Link groups

The standalone Env tab and the envelope expanded from Play publish through `EnvUi::publishQGroup` (`shell-mpc/overlay/env_ui.h`). Targets follow the selected envelope. The instance selector has no Q target.

### 1. Full-Screen Mode (`EnvFullScreen`, `kMaxGroups = 3`, `shell-mpc/ui/env_screen.h:197-200`)
- **Group 0: "ADSR"** (`shell-mpc/ui/env_screen.h:197`, `shell-mpc/overlay/env_ui.h:1116-1121`):
  * Q1: `ATTACK` (`env1.attack` in ms, or `env1.attack_beats` in beats)
  * Q2: `DECAY` (`env1.decay` in ms, or `env1.decay_beats` in beats)
  * Q3: `SUSTAIN` (`env1.sustain`)
  * Q4: `RELEASE` (`env1.release` in ms, or `env1.release_beats` in beats)
- **Group 1: "SHAPE"** (`shell-mpc/ui/env_screen.h:198`, `shell-mpc/overlay/env_ui.h:1116-1121`):
  * Q1: `A CURVE` (`env1.attack_curve`)
  * Q2: `D CURVE` (`env1.decay_curve`)
  * Q3: `R CURVE` (`env1.release_curve`)
  * Q4: Unused (idle)
- **Group 2: "TIME"** (`shell-mpc/ui/env_screen.h:199`, `shell-mpc/overlay/env_ui.h:1116-1121`):
  * Q1: `DELAY` (`env1.delay` in ms, or `env1.delay_beats` in beats)
  * Q2: `HOLD` (`env1.hold` in ms, or `env1.hold_beats` in beats)
  * Q3: `BEATS` (`env1.units`)
  * Q4: RETRIG (`env1.legato_inverted`)

On four-knob hardware, the active group is bank 1 and the following two groups wrap through banks 2 and 3. On the standalone Env tab, touching a native control selects its Q-Link group. The host handles the four ADSR dials, three curve dials, Delay, Hold, Beats and Retrig. The five time controls follow the selected envelope’s units. The skin draws TARGETS and the overlay opens the selected envelope’s destination list. The graph and the used/free instance selector are overlay controls. See `scripts/mpc-skin-extras/96_env_native.py` and `shell-mpc/overlay/env_ui.h`.
On XL, ADSR occupies Q1–Q4, SHAPE Q5–Q8 and TIME Q9–Q12. Q8 and Q13–Q16 are empty.

### 2. Compact View Mode (Play Page Middle Third, `shell-mpc/overlay/env_ui.h:1069-1070`)
- The ADSR Q group occupies Q5–Q8 on XL and bank 1 on Key 61 when the envelope third is active. The native Play time dials and Q targets follow the selected envelope's time units. See [Play](play.md#q-link-groups), `shell-mpc/overlay/play_view.h` and `shell-mpc/play_params.h`.
  * Q1: `ATTACK` (`env1.attack` or `env1.attack_beats`)
  * Q2: `DECAY` (`env1.decay` or `env1.decay_beats`)
  * Q3: `SUSTAIN` (`env1.sustain`)
  * Q4: `RELEASE` (`env1.release` or `env1.release_beats`)

### 3. Detail Pages
- **Targets Page** (`pageTop_ == kPageTargets`, `shell-mpc/overlay/env_ui.h:1100-1105`): Published as "Targets", mapping rows of the modulation matrix: Q1 = `mod01.amount`, Q2 = `mod01.bipolar`, Q3 = `mod01.bypass`, Q4 = unused.
- **Modulations Page** (`pageTop_ == kPageMods`, `shell-mpc/overlay/env_ui.h:1100-1105`): Published as "Mods", with the same amount, polarity and bypass positions per incoming modulation row. Q4 is empty.
- **List Popup** (`pageTop_ == kPageList`, `shell-mpc/overlay/env_ui.h:1111-1113`): Published as "List" with 0 active Q-slots.

## Live parts

The Envelope page renders continuous real-time animations driven by audio engine state (`shell-mpc/overlay/env_ui.h:188-197, 549-565`, `shell-mpc/ui/env_graph.h:247-273`):

1. **Running Voice Progress Dots**:
   - In `EnvGraph::drawLive()` (`shell-mpc/ui/env_graph.h:247-273`), up to 8 sounding voices (`LiveValues::kReadVoices`) are rendered along the active envelope curve.
   - Stage tracking: Positions follow `live->envVoiceStage[e][i]` (stages 1..6: Delay, Attack, Hold, Decay, Sustain, Release) and `live->envVoiceFraction[e][i]` (0.0..1.0 fraction of stage progress).
   - Visual styling: The newest voice is drawn on top with a dual-disc highlight (outer 4.6 px colored disc + inner 2.2 px white core disc); older voices are rendered as smaller 3.0 px semi-transparent discs (alpha 170).
   - Sustain stage behavior: On stage 5 (sustain), the dot remains stationary at the midpoint (50%) of the sustain segment at level `env1.sustain`.
   - Single-voice fallback: If multi-voice arrays are empty, the renderer uses fine readouts `envFineStage[e]` and `envFineFraction[e]`.

2. **Modulation Depth Arcs and Values**:
   - In `EnvUi::refreshMods()` (`shell-mpc/overlay/env_ui.h:570-580`), all knob dials update their modulation arc overlays in real time based on active mod matrix routings.

3. **Top Bar Modulation Readout Tags**:
   - In `EnvUi::refreshMods()` and `EnvFullScreen::drawExtra()` (`shell-mpc/overlay/env_ui.h:581-594`, `shell-mpc/ui/env_screen.h:303-333`), modulation destination tags dynamically update labels, amounts, and destination counts during live playback.

4. **Instance Target Indicator Dots**:
   - In `EnvUi::refreshMods()` (`shell-mpc/overlay/env_ui.h:595-609`), `EnvSelector` and `EnvCarousel` update small colored indicator dots indicating which envelopes actively drive modulation matrix slots.

## Duplicates

The following parameter keys controlled on this page also appear on other overlay views and host panels:

- `env1.attack`:
  * Play page compact envelope third (`shell-mpc/overlay/env_ui.h:359`, `shell-mpc/ui/env_screen.h:108`, `shell-mpc/overlay/play_view.h:490`).
  * Play page host proxy parameter "Play Env Attack" (`shell-mpc/play_params.h:41`).
  * Overlay tabs `env2`, `env3`, `env4` when switched to Envelope 1 via the `EnvSelector` (`shell-mpc/overlay/env_ui.h:347, 911-912`).
- `env1.decay`:
  * Play page compact envelope third (`shell-mpc/overlay/env_ui.h:359`, `shell-mpc/ui/env_screen.h:108`, `shell-mpc/overlay/play_view.h:490`).
  * Play page host proxy parameter "Play Env Decay" (`shell-mpc/play_params.h:41`).
  * Overlay tabs `env2`, `env3`, `env4` when switched to Envelope 1.
- `env1.sustain`:
  * Play page compact envelope third (`shell-mpc/overlay/env_ui.h:359`, `shell-mpc/ui/env_screen.h:108`, `shell-mpc/overlay/play_view.h:490`).
  * Play page host proxy parameter "Play Env Sustain" (`shell-mpc/play_params.h:41`).
  * Overlay tabs `env2`, `env3`, `env4` when switched to Envelope 1.
- `env1.release`:
  * Play page compact envelope third (`shell-mpc/overlay/env_ui.h:359`, `shell-mpc/ui/env_screen.h:108`, `shell-mpc/overlay/play_view.h:490`).
  * Play page host proxy parameter "Play Env Release" (`shell-mpc/play_params.h:42`).
  * Overlay tabs `env2`, `env3`, `env4` when switched to Envelope 1.
- `env1.hold`:
  * Play page host proxy parameter "Play Env Hold" (`shell-mpc/play_params.h:41`).
  * Overlay tabs `env2`, `env3`, `env4` when switched to Envelope 1.
- `env1.attack_curve`:
  * Play page host proxy parameter "Play Env Atk Curve" (`shell-mpc/play_params.h:42`).
  * Overlay tabs `env2`, `env3`, `env4` when switched to Envelope 1.
- `env1.decay_curve`:
  * Play page host proxy parameter "Play Env Dec Curve" (`shell-mpc/play_params.h:42`).
  * Overlay tabs `env2`, `env3`, `env4` when switched to Envelope 1.
- `env1.release_curve`:
  * Play page host proxy parameter "Play Env Rel Curve" (`shell-mpc/play_params.h:42`).
  * Overlay tabs `env2`, `env3`, `env4` when switched to Envelope 1.
- `env1.units`, `env1.delay`, `env1.legato_inverted`, `env1.attack_beats`, `env1.decay_beats`, `env1.release_beats`, `env1.delay_beats`, `env1.hold_beats`:
  * Available on overlay tabs `env2`, `env3`, `env4` whenever Envelope 1 is chosen via the `EnvSelector` (`shell-mpc/overlay/env_ui.h:347, 911-912`).
- Modulation routing parameters (`mod01.amount`, `mod01.bipolar`, `mod01.bypass`, `mod01.source`, `mod01.destination`):
  * Also accessible on the dedicated Mod Matrix tab (`shell-mpc/overlay/mod_ui.h`), LFO targets page (`shell-mpc/overlay/lfo_ui.h`), and filter modulation routing pages (`shell-mpc/overlay/filter_ui.h`).

## Questionable

1. **Dead `NumberField` / `T::Field` code in `EnvUi`** (`shell-mpc/overlay/env_ui.h:68, 423-437, 987`):
   - In `EnvUi::pull()` (`shell-mpc/overlay/env_ui.h:423-437`), there is dedicated handling for `T::Field` components (`NumberField` for DELAY, ATTACK, HOLD, DECAY, SUSTAIN, RELEASE) with unit string formatting.
   - However, `bind()` is never called with `T::Field` anywhere in `EnvUi::configure()` (`shell-mpc/overlay/env_ui.h:327-371`).
   - The comment in `shell-mpc/ui/env_screen.h:5-6` confirms that stage times are drawn directly under the graph and on the knobs, completely replacing separate number boxes. The `T::Field` code is obsolete dead weight and should be removed.

2. **Button label `LEGATO` writes inverted parameter `env1.legato_inverted`** (`shell-mpc/ui/env_screen.h:175, 190`, `shell-mpc/overlay/env_ui.h:362, 988`):
   - In `EnvFullScreen`, the toggle switch is given the on-screen label "LEGATO" (`legato.setLabel ("LEGATO")`).
   - In `EnvUi::configure()`, it is bound to `prefix_ + "legato_inverted"`, writing `1.0` when turned on and `0.0` when turned off.
   - Presenting an inverted engine parameter with a positive label is confusing: turning "LEGATO" on actually enables inverted legato behavior (retriggering on legato transitions).

3. **Four dedicated overlay tabs for 6 envelopes with redundant instance selectors** (`shell-mpc/overlay/overlay_layout.json:268-390`, `shell-mpc/ui/env_screen.h:28-73, 258-259`):
   - `overlay_layout.json` defines 4 dedicated tabs: `Env 1`, `Env 2`, `Env 3`, `Env 4`. But the engine has 6 envelopes (`kNumEnvs = 6`, `kEnvs = 6`). Envelopes 5 and 6 do not have their own tabs in the MPC tab strip and can only be reached via the on-screen `EnvSelector`.
   - Furthermore, every envelope tab features the complete 6-button `EnvSelector` bar at the top, allowing the user to select any envelope 1..6 on any tab (`pageSel_`). This renders the 4 separate tabs largely redundant.

4. **Top bar destination tags look like interactive buttons but are read-only** (`shell-mpc/ui/env_screen.h:180-186, 303-333`):
   - In `EnvFullScreen::drawExtra()`, modulation destination tags (e.g. `CUTOFF  +35%`, `+1`) are rendered in the top bar with bullet icons and formatted text.
   - They visually resemble clickable chips, yet they have no touch handling (`shell-mpc/ui/env_screen.h:303`: "text, no button outline: they are not touch targets; the TARGETS button opens the list"). Tapping them does nothing; only the `TARGETS` button opens the matrix list.

5. **Play compact view (`EnvCompact`) lacks `BEATS` toggle** (`shell-mpc/ui/env_screen.h:102-160`, `shell-mpc/overlay/env_ui.h:351, 358-362`):
   - `EnvCompact` includes ATTACK, DECAY, SUSTAIN, and RELEASE knobs, which follow `env1.units` (switching between milliseconds and beats).
   - However, the `BEATS` toggle switch only exists in `EnvFullScreen`. A user working in the compact view cannot see whether the envelope is in ms or beats mode, nor toggle the units without expanding to full screen.

6. **Group 1 ("SHAPE") leaves hardware Q-Link knob Q4 idle** (`shell-mpc/ui/env_screen.h:198`, `shell-mpc/overlay/env_ui.h:1116-1121`):
   - Group 1 maps only three controls: `A CURVE` (Q1), `D CURVE` (Q2), and `R CURVE` (Q3).
   - On 4-knob hardware (MPC Key 61, MPC One, MPC Live), knob Q4 is unassigned and does nothing when the SHAPE bank is active.

7. **Graph zoom hides out-of-bounds handles without touch access** (`shell-mpc/ui/env_graph.h:185-195`):
   - In `FREE` (unlocked) mode, when zoomed into the time scale, handles whose x coordinates lie past the right edge of the plot (`px > pl.right() + 1.0`) are not rendered and cannot be touched (`handleOffered()` returns false).
   - If a user zooms in to edit the attack curve, they cannot access or adjust decay or release handles without zooming back out or toggling `LOCKED`.

8. **Fixed Ghost Envelope 1 reference with no option to toggle or change source** (`shell-mpc/overlay/env_ui.h:507-527`, `shell-mpc/ui/env_graph.h:452-461`):
   - When editing Envelopes 2 through 6, a faint ghost curve of Envelope 1 is unconditionally drawn on the canvas. While intended as an amp envelope reference, there is no UI control to toggle the ghost off or select a different envelope as reference.
