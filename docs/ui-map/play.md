# Play page
Refreshed 2026-11-23 against the native compact controls and the four runtime Q groups.

The compact ENV and LFO graph corners have native Expand buttons. The MPC owns SHAPE, RATE, the BPM division, the standard HZ/BPM/KEY buttons, and RND STYLE, SYNC and STEREO. Tapping BPM again preserves the current straight, dotted or triplet setting. The standard buttons use handle 3348 and the RND SYNC menu uses 3309, so a late RND menu choice cannot edit a standard LFO. KEY leaves RATE passive and releases its Q-Link. The compact LFO drawer lists LFO 1–10 and RND 1–4, with the current source's name shown on the control. Choosing a standard LFO from an RND view also clears the random override, even when it was the last standard choice. The 2678 selector still reports and restores the same standard value for existing projects. Native menu input expires on a source, preset or tab change.

## Purpose

The Play page is the main synthesis view of the MPC overlay: ONE page of three compact views side by side (`PlayView`, `shell-mpc/overlay/play_view.h:55`): the oscillator (left third, `OscUi` compact view), the envelope (middle third, `EnvUi` compact view) and the LFO (right third, `LfoUi` compact view). Each third has its selector at the top, a display (the oscillator cycle, the envelope curve with the running dots, the LFO shape with the cursors) and a block of controls under it. The selectors choose which oscillator (Sub, A, B, C, Noise), envelope (ENV 1 .. 6) and LFO (LFO 1 .. 10) the compact page shows; the choice is the host's plugin parameters "Play Osc", "Play Env", "Play LFO" (`IInstanceView::playSelection`, `shell-mpc/overlay/play_view.h:81-82`). There is one Play page (the sub-tabs Play Env and Play LFO are gone, `shell-mpc/overlay/play_view.h:60`); the Macros tab is its own page (`docs/ui-map/macros.md`).

## Layout

- The view is `play1`, kind `play` (`shell-mpc/overlay/overlay_layout.json:47-56`). The layout file still lists the old region `{x: 23, y: 14, w: 1234, h: 190}`, but the view overrides it with the whole screen `(0, 0, 1280, 800)` (`PlayView::regionOverride`, `shell-mpc/overlay/play_view.h:218-222`): every coordinate of the page and of the touch is a screen pixel, the tab area is `(0, 110, 1280, 628)` (`play::kTabY`, `play::kTabH`, `shell-mpc/overlay/play_view.h:38`).
- Thirds (compact views, all `kTabY + 4` high `kTabH - 8`): oscillator `x = 8, w = 420` (`shell-mpc/overlay/osc_ui.h:112,117`), envelope `x = 440, w = 400` (`shell-mpc/overlay/env_ui.h:73,82`), LFO `x = 858, w = 408` (`shell-mpc/overlay/lfo_ui.h:98,109`). A touch on the left of x = 434 / 850 makes the third the active Q group (`shell-mpc/overlay/play_view.h:227-228`).
- The three thirds share one row table (`shell-mpc/ui/play_rows.h`): the selector row (`kSelectorH = 56`, `shell-mpc/ui/play_rows.h:16`), the display (`displayRect`, `shell-mpc/ui/play_rows.h:22`) with a header band at its top for the buttons (power, route, full screen; `kBandH`, `shell-mpc/ui/play_rows.h:31`), and the controls block that starts at `controlsLine` (`shell-mpc/ui/play_rows.h:59`; the controls height is `kControlsH = 232`, `shell-mpc/ui/play_rows.h:18`).
- Oscillator third (`OscillatorCompact`, `shell-mpc/ui/screen.h`): native selector on top; the display with the power button at the left, the route button (not on Sub) and the full screen button at the right of the band; under it the stacked `BasicControls` (the Q group, four cells).
- Envelope third (`EnvCompact`, `shell-mpc/ui/env_screen.h:102`): selector; the graph with the full screen button in its band; four native dials in two rows of two (ATTACK, DECAY; SUSTAIN, RELEASE).
- LFO third (`LfoCompact`, `shell-mpc/ui/lfo_screen.h:20`): selector; the display with the full screen button; for an LFO the stepper SHAPE, the RATE control and the knobs RISE and SMOOTH; for a random LFO (RND) the stepper STYLE, RATE, the stepper SYNC and the LED switch STEREO (a taller block, the display gets shorter: `kQuadRandomH`, `shell-mpc/ui/lfo_screen.h:60,73-84`).

The host draws the compact selectors, the OSC power, expand, route and Basic controls, the four envelope dials and the compact LFO controls listed above. The overlay draws the oscillator, envelope and LFO curves and their live indicators. The native ENV/LFO controls listed above publish bindings for the visible source and sync mode. Each compact selector opens the SDK MenuOverlay. The host reads the labels from the selector parameter’s replacement strings. Play and the standalone oscillator tabs share the same source menu. Its drawer handles selection and the adjacent list shows each source’s live ON/OFF state. Every row remains selectable when its oscillator is off. The native choices are the five oscillator sources, ENV 1–6, LFO 1–10 and RND 1–4. The OSC cells are bound separately for each of the five sources. See `scripts/mpc-skin-extras/82_play.py` and `shell-mpc/overlay/play_view.h`.

## Controls

Only the controls of the compact views. Keys are given for the first instance of each kind (`osc1` = Osc A, `env1`, `lfo1`; B, C, `env2` .. `env6`, `lfo2` .. `lfo10`, `rlfo1` .. `rlfo4` follow the selector). The Q column says where the control sits on the Q-Links (see Q-Link groups).

| Label on screen | Parameter key | Gesture | Effect | Q | Source |
|---|---|---|---|---|---|
| Oscillator selector | Play Osc | Tap the native menu | Selects Sub, Osc A, Osc B, Osc C or Noise | — | `scripts/mpc-skin-extras/82_play.py` |
| Power icon | `osc1.on` (`sub.on`, `noise.on`) | Tap | Switches the oscillator on / off (red when off) | Q2 of the extra group | `shell-mpc/overlay/osc_ui.h:492` |
| Route button (F1, F2, ...; not on Sub) | `osc1.destination` (`noise.destination`) | Tap | Chooses a destination in the MPC menu or opens the balance and sends through Routing controls | none | `shell-mpc/overlay/osc_ui.h:494` |
| Full screen button (four corners) | - | Tap | Opens the oscillator full screen page | - | `shell-mpc/overlay/osc_ui.h:490` |
| Type / waveform | `osc1.type` (A, B, C), `sub.waveform` (Sub) | Tap the native menu / Q-Link | Chooses the engine type (WAVE, MULTI, SAMPLE, GRAIN, SPEC) or the sub waveform | Q1 of the oscillator group | `shell-mpc/overlay/osc_ui.h:500` |
| SOUND (Noise) | `noise.sample` | Tap the native menu / Q-Link | Chooses the noise kind or sample | Q1 | `shell-mpc/overlay/osc_ui.h:502` |
| Table / source | `osc1.wavetable` (A, B, C) | Tap arrows, tap the name opens the table / content browser | Chooses the wavetable or the content file | Q2 | `shell-mpc/overlay/osc_ui.h:508` |
| OCT (Sub) | `sub.octave` | Tap the keypad / Q-Link | Octave -4 .. +4 | Q2 | `shell-mpc/overlay/osc_ui.h:515` |
| FILTER (Noise) | `noise.filter` | Drag / Q-Link | Noise filter | Q2 | `shell-mpc/overlay/osc_ui.h:517` |
| PAN | `osc1.pan` (`sub.pan`, `noise.pan`) | Q-Link / native drag | Pan | Q3 | `shell-mpc/play_params.h` |
| LEVEL | `osc1.level` (`sub.level`, `noise.level`) | Q-Link / native drag | Level | Q4 | `shell-mpc/play_params.h` |
| Envelope selector | Play Env | Tap the native menu | Selects ENV 1–6 | — | `scripts/mpc-skin-extras/82_play.py` |
| Envelope graph | `env1.attack`, `env1.decay`, `env1.sustain`, `env1.release` (and the curves, delay, hold on the full screen) | Drag the handles, tap LOCK | Edits the shape; LOCK keeps the zoom | - | `shell-mpc/overlay/env_ui.h:348,855` |
| Envelope full screen button | - | Tap | Opens the envelope full screen page | - | `shell-mpc/overlay/env_ui.h:344,850` |
| ATTACK | `env1.attack` (`env1.attack_beats` while the units are beats, `env1.units`) | Drag / Q-Link | Attack time | Q1 of the envelope group | `shell-mpc/overlay/env_ui.h:357-359` |
| DECAY | `env1.decay` or `env1.decay_beats` | Drag / Q-Link | Decay time | Q2 | `shell-mpc/play_params.h` |
| SUSTAIN | `env1.sustain` | Drag / Q-Link | Sustain level | Q3 | `shell-mpc/overlay/env_ui.h:357-359` |
| RELEASE | `env1.release` or `env1.release_beats` | Drag / Q-Link | Release time | Q4 | `shell-mpc/play_params.h` |
| LFO selector | Play LFO Choose | Tap the native menu | Selects LFO 1–10 or RND 1–4 | — | `scripts/mpc-skin-extras/82_play.py` |
| LFO full screen button | - | Tap | Opens the LFO full screen page | - | `shell-mpc/overlay/lfo_ui.h:484,2104` |
| SHAPE (LFO) | `lfo1.shape` | Tap the native list or turn Q1 | Chooses the LFO shape | Q1 of the LFO group | `shell-mpc/overlay/lfo_ui.h:485` |
| STYLE (RND) | `rlfo1.style` | Tap the native list or turn Q1 | Chooses the random style | Q1 | `shell-mpc/overlay/lfo_ui.h:486` |
| RATE (knob, division stepper, mode cells) | `lfo1.rate`, `lfo1.division`, `lfo1.sync` (the HZ / BPM / KEY cells) | Drag the native dial, open the division list, tap the mode buttons | Rate, tempo division, sync mode | Q2 (the knob) | `shell-mpc/overlay/lfo_ui.h:487-489,2181-2187` |
| RISE (LFO) | `lfo1.fade_in` | Drag / Q-Link | Fade-in time | Q3 | `shell-mpc/overlay/lfo_ui.h:490` |
| SMOOTH TIME (LFO) | `lfo1.smooth_time` | Drag the native dial / Q-Link | Sets smoothing time | Q4 | `scripts/mpc-skin-extras/82_play.py` |
| SYNC (RND) | `rlfo1.sync` | Tap the native list or turn Q3 | Sync mode of the random LFO | Q3 | `shell-mpc/overlay/lfo_ui.h:492` |
| STEREO (RND) | `rlfo1.stereo` | Tap the LED toggle / Q-Link | Stereo of the random LFO | Q4 | `shell-mpc/overlay/lfo_ui.h:493` |

## Opens

| Control / gesture | Opens | Description |
|---|---|---|
| Oscillator selector | Native menu | Sub, Osc A, Osc B, Osc C, Noise |
| Oscillator full screen button | oscillator full screen page | The same page as the Osc tabs (`docs/ui-map/osc.md`), `stack.openFull (kElementOsc)` (`shell-mpc/overlay/osc_ui.h:1404`) |
| Route button | route popup | DEST, F1 F2, BUS 1, BUS 2 (`docs/ui-map/osc.md`): the compact view's shortcut (intentional, like LEVEL and PAN); the standalone Osc tabs use a native destination menu while the oscillator is on |
| Type pictogram, source name | type list, table / content browser | As on the Osc page |
| Envelope selector | Native menu | ENV 1–6 |
| Envelope full screen button | envelope full screen page | `docs/ui-map/env.md` (`stack.openFull (kElementEnv)`, `shell-mpc/overlay/env_ui.h:850`) |
| LFO selector | Native menu | LFO 1–10 and RND 1–4. |
| LFO full screen button | LFO full screen page | `docs/ui-map/lfo.md` (`stack.openFull (kElementLfo)`, `shell-mpc/overlay/lfo_ui.h:2104`) |
| LFO SHAPE / STYLE / RND SYNC / BPM division | Native menu | Opens from the compact view. |

The expanded OSC, ENV and LFO/RND editors mount the same MPC control groups as their standalone tabs. The OSC selector changes the current Play oscillator and can select a disabled source. A disabled oscillator keeps POWER, its selector and CLOSE available. Play ENV and LFO/RND keep their selected preset sources. The overlay still draws the graphs and owns their custom gestures. The native CLOSE returns to the compact view and restores its Q-Link targets. A detail’s BACK returns to its expanded editor.

## Q-Link groups

Compact Play publishes four groups. An expanded view publishes its own targets until it closes. The slot order below comes from `PlayView::publishQ` and `extraTargets` in `shell-mpc/overlay/play_view.h`.

| Group | Q1 | Q2 | Q3 | Q4 | Source |
|---|---|---|---|---|---|
| 1 Oscillator | type / waveform / noise SOUND | table or source / OCT (Sub) / FILTER (Noise) | PAN | LEVEL | `shell-mpc/ui/composite.h:659-663`, `shell-mpc/overlay/osc_ui.h:2052` |
| 2 Envelope | ATTACK `env1.attack` or `env1.attack_beats` | DECAY `env1.decay` or `env1.decay_beats` | SUSTAIN `env1.sustain` | RELEASE `env1.release` or `env1.release_beats` | `shell-mpc/ui/env_screen.h:121`, `shell-mpc/overlay/env_ui.h:1069` |
| 3 LFO | SHAPE `lfo1.shape` (RND: STYLE `rlfo1.style`) | RATE `lfo1.rate` or BPM DIVISION `lfo1.division` (RND: `rlfo1.rate` or `rlfo1.division`) | RISE `lfo1.fade_in` (RND: SYNC `rlfo1.sync`) | SMOOTH `lfo1.smooth_time` (RND: STEREO `rlfo1.stereo`) | `shell-mpc/ui/lfo_screen.h:49-53`, `shell-mpc/overlay/lfo_ui.h:2318` |
| 4 Extra (Q-Link mapping extension, 2026-11-21) | LFO SYNC `lfo1.sync` (RND: `rlfo1.sync`) | OSC POWER `osc1.on` / `sub.on` / `noise.on` | LFO MONO `lfo1.mono` (standard LFO only: for a RND LFO the member is empty by design, there is no mono parameter of a random LFO) | - (empty) | `PlayView::extraTargets`, `shell-mpc/overlay/play_view.h:373-382` |

- **MPC XL (16 knobs, no active group):** four groups keep their slots (`QRouter::blockOfGroup`, `shell-mpc/q_router.h:71-79`): oscillator Q 1 .. 4, envelope Q 5 .. 8, LFO Q 9 .. 12, extra Q 13 .. 16 when applicable. Q16 stays empty. The four groups keep their positions under touch.
- **Key 61 (four knobs, banks):** the group of the touched third (`highlight_`) is bank 1, the others follow in reading order and wrap (`shell-mpc/overlay/play_view.h:361-368`): osc active = osc, env, LFO, extra; env active = env, LFO, extra, osc; LFO active = LFO, extra, osc, env. The extra group is bank 4 only when the oscillator is active; chips are drawn for groups 1 .. 3 (`setCompactBlock`, `shell-mpc/overlay/play_view.h:362-364`); the extra group has NO chips (its controls are the compact views' own).
- The three extra members are STEPPED targets: a Q-Link moves them one step per 6 clicks (`QRouter::hostWrite`, `shell-mpc/q_router.h:202`), so one click cannot switch an oscillator off (`shell-mpc/tests/q_step_rule_host.cpp` `testNudge`: 1 and 5 clicks change nothing, the 6th makes one step; `docs/ui-map/q-link-audit.md`).
- Slot 4 of the extra group is empty. `testPlayPageQSlots` covers both layouts in `shell-mpc/overlay/tests/env_ui_host.cpp`. `shell-mpc/overlay/tests/gpu_scene_host.cpp` checks the compact native apertures and the active Q frame.
- On all five enabled sources, the native PAN/LEVEL widgets and Q slots reach the selected oscillator parameters. When the source is off, PAN and LEVEL become grey readouts. The selector and POWER remain available and the other controls reject edits. The real PAN/LEVEL parameters still accept automation and preset changes. Sub and Noise keep their older proxy aliases. The three Env time dials and the ADSR Q group follow the selected envelope's BEATS switch. The host refreshes the native display on a unit change, including when the normalized value is identical. Sources: `scripts/mpc-skin-extras/82_play.py`, `shell-mpc/play_params.h` and `shell-mpc/synthyboy_mpc.cpp`. Mac VST coverage: `shell-mpc/tests/play_host.cpp`.
- The three selectors, expand buttons, route button and envelope graph have no Q target of their own. The extra group gives OSC POWER and LFO SYNC their Q targets. Compact SMOOTH maps the time only.

## Live parts

- **Oscillator cycle**: the display rectangle of the compact view is drawn from the engine's oscillator cycle (`OscDisplay`, `shell-mpc/overlay/play_view.h:106`; drawn at `shell-mpc/overlay/play_view.h:484-494`). The view's hash includes the cycle samples (`shell-mpc/overlay/play_view.h:323-335`).
- **Envelope graph**: the curve is computed from the selected envelope's parameters (`shell-mpc/overlay/play_view.h:107-120`); the running dots of the voices come from `live->envFineStage` / `envFineFraction` (`shell-mpc/overlay/play_view.h:121-130`) and are drawn by `EnvUi::drawLive` (`shell-mpc/overlay/play_view.h:510`).
- **LFO curve**: the shape from `LfoDisplay::compute` with the live modulation sources for a random LFO (`shell-mpc/overlay/play_view.h:132-161`); the cursor from `live->lfoPhase` (`shell-mpc/overlay/play_view.h:162-169`), drawn by `LfoUi::drawLive` (`shell-mpc/overlay/play_view.h:518`).
- **Q state**: the page re-publishes its Q slots after every update and when a full screen closes (`shell-mpc/overlay/play_view.h:190-205`).

## Duplicates

| Parameter key | Also on |
|---|---|
| `osc1.on` / `sub.on` / `noise.on` | Oscillator full screen (`shell-mpc/overlay/osc_ui.h:493`), Mixer strip switch (`shell-mpc/overlay/mixer_ui.h:321-325`), the extra Q group |
| `osc1.destination` | standalone Osc tab native destination menu (`scripts/native_osc/routing.py`), route popup (`shell-mpc/overlay/osc_ui.h:815`), Mixer (`shell-mpc/overlay/mixer_ui.h:321-325`) |
| `osc1.type`, `sub.waveform`, `osc1.wavetable`, `sub.octave`, `noise.sample`, `noise.filter` | Oscillator full screen (`shell-mpc/overlay/osc_ui.h:500-517`, the same `BasicControls` cells) |
| `osc1.pan`, `osc1.level` (and the Sub / Noise ones) | Oscillator full screen (`shell-mpc/overlay/osc_ui.h:518-519`), Mixer strips (`shell-mpc/overlay/mixer_ui.h:321-325`) |
| `env1.attack`, `env1.decay`, `env1.sustain`, `env1.release` | Envelope full screen (`shell-mpc/overlay/env_ui.h:357-359`, the same knobs) |
| `lfo1.shape`, `rlfo1.style`, `lfo1.rate`, `lfo1.division`, `lfo1.fade_in`, `lfo1.smooth_time`, `lfo1.smooth`, `rlfo1.stereo` | LFO full screen (`shell-mpc/overlay/lfo_ui.h:497-523`) |
| `lfo1.sync` (`rlfo1.sync`) | LFO full screen sync switch (`shell-mpc/overlay/lfo_ui.h:498-500,2181-2187`), the extra Q group |
| `lfo1.mono` | LFO full screen (`shell-mpc/overlay/lfo_ui.h:519`); on the Play page only through the extra Q group (no compact control) |

## Questionable

1. **LFO MONO is empty for random LFOs (settled 2026-11-21)**: the parameter table has no mono parameter for the random LFOs, so `PlayView::extraTargets` builds the MONO member only for a standard LFO; for a RND LFO the extra group is [LFO SYNC, OSC POWER, -, -] by design (`shell-mpc/overlay/tests/env_ui_host.cpp` `testPlayPageSelectorsAndExtraGroup`: all four random LFOs, and that the table has no such key).
2. **Extra group members without a control on the page**: LFO MONO has no compact control, OSC POWER duplicates the power icon, LFO SYNC duplicates the mode cells; there are no chips, so the player cannot see which knob does what (`shell-mpc/overlay/play_view.h:370-372`).
3. **Region in the layout file is not the real one**: `overlay_layout.json` still says `{23, 14, 1234, 190}` while the view overrides it with the whole screen (`shell-mpc/overlay/overlay_layout.json:47-56`, `shell-mpc/overlay/play_view.h:218-222`); any tool that reads the file alone gets the old size.
4. **Selector as a Q target not decided** (owner decision, `docs/ui-map/q-link-audit.md`): the fourth slot of the extra group stays empty.
5. **Dimmed vs hidden on the LFO compact view**: SHAPE, RISE and SMOOTH are shown for an LFO and hidden for a random LFO (`LfoCompact::configure`, `shell-mpc/ui/lfo_screen.h:43-54`), which is the hidden-not-dimmed rule; the full screen's `smooth_time` and `edit` for random LFOs are covered in `docs/ui-map/lfo.md`.
6. **Unused selection in the overlay selector.** On the Env and LFO tabs an unedited `+` selection falls back when leaving the tab (`uibind::fallbackSelection`). The compact native menus list fixed choices. The tests `testPlayCarouselIsTheTabSelector` exercise the shared overlay selection model.

## Play előnézeti képek és réteghatárok

- `play-preview-overlay-only.png` és `play-sub-preview-overlay-only.png`:
  Kizárólag a SynthyBoy grafikus overlay rétegének (hullámforma, ADSR burkológörbe, LFO hullámalak) offline tesztkörnyezeti előnézete (render: valós C++ `PlayView` + `scene_cpu.h` overlay renderer, `shell-mpc/overlay/tests/play_render_fixture.cpp`).
  **Nem tartalmazza** a fizikai Akai MPC natív felületét és skin kezelőszerveit (keretek, hátterek, natív 90 px Sub tárcsák, választók, gombok).
- `../screens/play.jpg` és `docs/ui-components/komponensek/play-teljes-kepernyo.png`:
  **Offline szintetikus fixture kompozit** (1280 × 628 tab; **nem** fizikai MPC hardveres képernyőmentés):
  - **Natív skin réteg**: a legfrissebb TUI.json skinből generálva (`scripts/render_play_native_snapshot.py`), amely a `render-mpc-skin-preview.py` alosztályozásával (`PlaySketch`) valósítja meg a helyes működést:
    * Modulfejlécek és klipping: mindhárom modulfejléc a tényleges befoglaló keretére van méretezve (`Play Osc Select` 338×56 -> SUB, `Play Env Select` 388×56 -> ENV 1, `Play LFO Select` / `Play Native EnvLfo Select` 338×56 -> LFO 1), jobb oldali nyíl- és keretlevágás nélkül. Megjegyzés a referenciához képest: `playOscSelector` továbbra is `neutral=True` (`#202020`, `test_mpc_skin_overlay.py:654` rögzíti), míg az `ENV 1`/`LFO 1` fejléc saját kék (`#0e1b33`), és a `modulfejlec-referencia.png` mindhárom fejlécet egységesen kékben, bal/jobb szomszédcímkékkel mutatja.
    * Állapotszűrés és komponens hátterek: inaktív és módosító komponensek szűrése (`FIXTURE_STATE` alapján az Osc A/B/C/Noise, bővített szerkesztők és modulációs nézetek elrejtve; pontosan a legitim kompakt Play elemek aktívak), valamint az egyedi komponens definíciók valós `backgroundData` kitöltése (így a natív ENV tárcsák `#060913` háttere elfedi a fallback hátlapi címkéket, egyetlen címke + érték marad).
    * Képarány-tartó tekerők és saját kék FilmStrip komponensek: minden Play tárcsa (`playSubPan`, `playSubLevel`, `compactNativeDial`, `compactNativeRiseDial`, `compactNativeSmoothWithIndicator`, `compactNativeSmoothTime`, `playEnvLfoRate`) közvetlenül a `components/native/level/level.py` (`play_sub_level_90.png`, `play_sub_level_60.png`, `play_sub_level_40.png`) és `components/native/pan/pan.py` (`play_sub_pan_90.png`) jóváhagyott kék 127 képkockás `FilmStrip` komponensét használja saját `Name`/`Value` címkékkel (`0.0 ms`, `1.00 s`, `50.0 %`, `500 ms`, `1.00 Hz`, `150 ms`). A gyári piros `knob_uni.png` fallback és alapértelmezés szigorúan tiltott és elutasított.
    * `SMOOTH` mező (LFO): a korábbi idegen slider-pill kapcsoló (`compactNativeSmoothToggle`, `Play LFO Smooth Switch` az `[1184, 546, 76, 72]` pozíción) megszűnt. Helyette a Play LFO simítás közvetlenül a jóváhagyott közös `ActiveText` (`components/native/active_text/active_text.py`) és a 60 px-es `Level` FilmStrip tárcsa integrációjával valósul meg a teljes, pill eltávolításával felszabadult 196×72 px-es cellában (`compactNativeSmoothWithIndicator` aktív állapotban, `compactNativeSmoothReadout` passzív állapotban, `[1064, 546, 196, 72]`).
      A 196×72-es szülőcellán belül három szigorúan szétválasztott, egymást nem átfedő (diszjoint) kezelőterület található:
      1. **TIME tárcsa** (helyi téglalap: `[2, 6, 60, 60]`, abszolút X: 1066..1126): ON állapotban a saját kék 60 px-es dial (`play_sub_level_60.png`, kék aktív ív), OFF állapotban a LEVEL/Smooth tokenekből (`smooth.track.off`, `smooth.arc.off`, `smooth.pointer.off`) származtatott szürke inaktív dial (`play_sub_level_60_inactive.png`), passzív bemenettel (`dragOrientation: "None"`, `ignoreMousePresses: True`, `acceptsHWFocus: "No"`).
      2. **ActiveText Button** (helyi téglalap: `[66, 8, 60, 56]`, abszolút X: 1130..1190): valós érintési hitbox mindkét irányban >= 56 px (`60×56` px), a `sizes.json` névvel rögzített tokenjei alapján (`smooth.caption.hitbox.width: 60`, `smooth.caption.hitbox.height: 56`). A vizuális felirat változatlan betűmérettel, középre igazítva helyezkedik el a nagyobb átlátszó gomb belsejében. A gomb szigorúan el van választva a tárcsától és az értékmezőtől (4 px rés mindkét oldalon), így a passzív címke/érték nem foghatja el a kattintást. Színe ON állapotban világos cián `#00ffc2`, OFF állapotban szürke `#384252` (`active_text.off`). Egyetlen `active` on/off kapcsoló, nincs második állapotváltozó.
      3. **Érték címke** (helyi téglalap: `[130, 22, 62, 28]`, abszolút X: 1194..1256): formázott numerikus időérték (`150 ms`), ON állapotban fehér `#ffffff`, OFF állapotban dim szürke `#78889e` (`smooth.value.off`).
      A felirat mindkét irányban (ON -> OFF és OFF -> ON) kapcsolja a kiválasztott LFO `smooth` kapcsolóját az `Active` handle-n keresztül (`numButtonsInGroup: 1`, `buttonId: 1`). OFF állapotban az idő-írásvédelem teljes körű: sem a natív érintés/adatkerék, sem a VST proxy nem írhatja felül a simítási időt. A simítás csak a standard LFO-kra (1..10) vonatkozik; véletlen LFO (RND 1..4) esetén a Smooth mező rejtve marad (`is_smooth_applicable == False`).
      Kanonikus előnézeti képek: ON állapotban `docs/ui-components/komponensek/play-teljes-kepernyo.png` és `../screens/play.jpg`; OFF állapotban `docs/ui-components/komponensek/play-smooth-off.png` és `play-screen-smooth-off-preview.png`.
    * `WAVE` és `OCT` mezők (Sub): a `WAVE` választó (`playSubWaveField`, 292×72 px érintőterület, 292×56 px vizuális keret, `[14, 404, 292, 72]`) a felszabaduló 90 px-szel kiszélesítve befogadja az összes Sub hullámformanevet (`Rounded Rect`). Mellette a kompakt `OCT` mező (`playOscNumberField`, `[310, 412, 112, 56]` érintőterület, `40 px` vizuális magasság, `68×40` px értékdoboz) a jóváhagyott közös `components/native/name_value/name_value.py` komponensből épül (cím `OCT` kívül balra, csak a `-4..4` szám a keretben), megőrizve a `-4..4` numerikus listás (`play osc octave`) bemenetet, a Q2 Q-Link kötést és a Sub OFF írásvédelmet (`playSubWaveInactive`, `playSubOctInactive`). Az OSC A/B/C és Noise mezők geometriája (`202×72` px) változatlan.
    * `SHAPE` mező (LFO): a `SHAPE` (`playEnvLfoChoice SHAPE`) a `build_shape_selector(..., arrows=True)` jóváhagyott változatot használja (`shape-mezo-referencia.png`: középre igazított `SHAPE` + `Sine`, bal/jobb nyilak, külön `v` jel nélkül).
    * Natív `Image` filmstrip értékelés: az `Overlay Instance play1` (33 képkockás `overlay_instance_6x6.png`) csak a fixture állapot szerinti egyetlen `6×6` képkockát rajzolja ki.
    * Aktív gombállapotok: Sub táp BE, SYNC HZ BE (`radio_button` / `button` natív eszközök).
  - **Overlay réteg**: a C++ `PlayView` + `scene_cpu.h` valódi, jelen worktree forrásából fordított (`--build-dir` / per-checkout `/tmp/synthyboy-<uid>/<cksum>/play-fixture-core` `CMakeCache.txt` forrásazonosítással) és futtatott renderéből (`shell-mpc/overlay/tests/play_render_fixture.cpp`, `PlayFixtureInstance` natív Play OSC/ENV/LFO tulajdonlással) származó valós belső hullámforma-, ADSR- és LFO-görbék, valamint a `native_marker_covers.h` (`drawNativeMarkerCovers(..., "play1", ...)`) által fedett layout-markerek. Történeti PNG (`Init_play1_src_sub.png` vagy `docs/user-guide/img/play.png`) nem képezi a futási útvonal részét.
  - **Referenciahelyzet**: a repóban jelenleg **nem azonosítható jóváhagyott teljes képernyős natív Play referencia-képernyőmentés** (a történeti `Init_play1_src_sub.png` és `docs/user-guide/img/play.png` Q-Link számokkal ellátott korábbi C++ overlay-képek, nem natív skin-referenciák; fizikai hardveres kép csak a kiadáskor készül). Komponensszintű jóváhagyott referenciák léteznek (`modulfejlec-referencia.png`, `shape-mezo-referencia.png`, `components/native/level/catalog.json`, `components/native/pan/catalog.json`, `components/native/button/catalog.json`, `components/native/radio_button/catalog.json`, `components/native/selector/catalog.json`), valamint az elrendezési szerződések (`play_rows.h`, `play-osc-native-contract.json`, `play-env-lfo-contract.json`).
  - **Apertúrák és kompozit**: `render_play_screen_preview.py` teljes 1280×628 alfa-kompozitot (`Image.alpha_composite`) végez a natív réteg és a C++ overlay között; minden natív választó, gomb (`Sub power`, `Sub expand`, `Env expand`, `LFO expand`) és tárcsa helyén a C++ réteg átlátszó (`alpha == 0`).
  - **Szabályok betartása**: szigorúan 0 látható Q-Link szám vagy keret (Rule 5b), 0 fantom badge vagy vízjel, paraméterenként egyetlen bemeneti út.
  - **Kimeneti izoláció**: a `--out <útvonal>` kapcsoló kizárólag a kért célfájlt írja, a kanonikus dokumentációs képeket nem érinti. A kanonikus képek frissítése explicit `--canonical` kapcsolóval vagy default futtatással történik, szükség esetén létrehozva a szülőkönyvtárakat.
- **Fizikai hardveres képernyőmentés:**
  A fizikai Akai MPC hardverre történő hivatalos kiadási telepítés és tesztelés során rögzítendő (`docs/release/`). A fejlesztési fázisban a direkt deploy tiltott.
