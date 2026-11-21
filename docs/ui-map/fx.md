# FX Page UI Map

## Purpose

The FX page provides complete visual control over SynthyBoy's effect rack and routing architecture (`shell-mpc/overlay/fx_ui.h:1-16`). It manages effects across the primary mix racks (Main rack, Bus 1, Bus 2) and the dedicated band racks 3 through 9 used by splitters (`shell-mpc/ui/fx_layout.h:202-240`). Users can audition and inspect effects in processing order, drag cards to reorder DSP modules, toggle individual bypass/power states, move effects between racks, remove instances, monitor real-time filter response curves, waveshaper curves, compression transfer characteristics, and delay echo taps, and edit all DSP parameters using touch controls and Q-Link hardware encoders (`shell-mpc/overlay/fx_ui.h:120-157`).

## Layout

The FX view occupies the full tab canvas defined in `shell-mpc/overlay/overlay_layout.json:527-563` (tab "FX", kind "fx", index 1, region `x: 0, y: 0, w: 1280, h: 628`, tab screen offset `y = 110`, opaque rendering).

The screen layout (`shell-mpc/ui/fx_screen.h:63-70`) consists of two main vertical regions plus a modal popup layer:

1. **Chain Bar (`FxChainBar`, `shell-mpc/ui/fx_screen.h:24`, `shell-mpc/ui/fx_chain_bar.h:314-430`):**
   - Placed across the top of the tab canvas (`y = 118`, height 88 px, width 1264 px).
   - Contains slot cards (`FxSlotCard`) representing active effect modules in processing order, a `+ ADD` card (`plusCard`) to insert effects, quiet spare add cards (`spare`) filling extra width, and an optional `BACK` chevron card (`backCard`) displayed when navigated into a splitter band rack (`scope_ != 0`).
   - Cards display rack badges ("MAIN", "BUS 1", "BUS 2"), a type pictogram, a power toggle icon, and the effect name.

2. **Main Content Area (`y = 214..730`):**
   - **Empty Chain Gallery (`FxGallery`, `shell-mpc/ui/fx_gallery.h:23-298`):** Shown when no active effects exist in the chain (`shell-mpc/ui/fx_screen.h:42,59`). Displays headline "TAP AN EFFECT TO ADD IT TO THE CHAIN" and two rows of cards for all available effect types (13 pool types + 3 splitters), with authentic graphic transfer curves and response shapes (`shell-mpc/ui/fx_gallery.h:100-234`).
   - **Slot Editor (`FxSlotEditor`, `shell-mpc/ui/fx_slot_editor.h:32-425`):** Shown when an effect is selected.
     - **Header (`shell-mpc/ui/fx_slot_editor.h:314-325`, height 56 px):** Includes an effect power toggle icon, path breadcrumbs (e.g. "MAIN CHAIN > SPLITTER L/M/H > MID BAND"), effect title, optional notice text (e.g. missing impulse file name), a `RACK` stepper, a `PAGE` stepper (visible if rows exceed one page), and a `REMOVE` button.
     - **Display Area (`shell-mpc/ui/fx_slot_editor.h:329-338`):**
       - `FxDisplay` (`shell-mpc/ui/fx_display.h:22-458`): 160 to 220 px high for EQ, Compressor, Delay, FX Filter, and Distortion. Shows real-time biquad response curves, multi-band compression transfer curves, delay repeat echo taps, or waveshaper transfer curves with live input meter dots.
       - `FxBandBoard` (`shell-mpc/ui/fx_band_board.h:77-192`): 206 to 240 px high for Splitter modules (L/H, L/M/H, M/S). Displays 2 or 3 band lanes with band tiles (`FxBandTile`), band effect slot cards, and band `+ ADD` cards.
     - **Knob Area (`shell-mpc/ui/fx_slot_editor.h:339-394`):** Rows of 4 cells holding knobs, steppers, LED lists, LED toggles, and integer number fields corresponding to the active row blocks of the effect table (`shell-mpc/ui/fx_layout.h:63-76`). Trailing inactive controls are hidden; leading inactive controls are drawn dim without touch response. Above each cell is a dedicated lane for Q-Link group index chips.

3. **Modal Popup Layer (`ListPopup`, `shell-mpc/ui/list.h:20-60`, `shell-mpc/overlay/fx_ui.h:97,858-933`):**
   - Renders above the screen canvas when adding an effect or tapping an extended parameter choice stepper.

## Controls

| Label on screen | Parameter key | Gesture | Effect | Source |
| --- | --- | --- | --- | --- |
| SLOT CARD | - | Tap | Selects effect slot for editing on page | `shell-mpc/ui/fx_chain_bar.h:114` |
| POWER | - | Tap power icon | Toggles effect slot active power state on or off | `shell-mpc/ui/fx_chain_bar.h:76` |
| SLOT REORDER | - | Drag horizontally | Moves card to new processing position in rack | `shell-mpc/ui/fx_chain_bar.h:110` |
| + ADD | - | Tap | Opens type selector list popup to add new effect | `shell-mpc/ui/fx_chain_bar.h:101` |
| + ADD (SPARE) | - | Tap | Opens type selector list popup to add new effect | `shell-mpc/ui/fx_chain_bar.h:376` |
| BACK | - | Tap | Returns from band chain view to parent splitter | `shell-mpc/ui/fx_chain_bar.h:104` |
| TYPE CARD | - | Tap | Adds effect of chosen type to end of Main rack and selects it | `shell-mpc/ui/fx_gallery.h:258` |
| POWER | - | Tap | Toggles active power of currently selected effect | `shell-mpc/ui/fx_slot_editor.h:316` |
| RACK | - | Tap left/right or turn | Reassigns effect to MAIN, BUS 1, BUS 2, or band rack | `shell-mpc/ui/fx_slot_editor.h:325` |
| PAGE | - | Tap left/right or turn | Switches parameter page when effect has more rows than fit one page | `shell-mpc/ui/fx_slot_editor.h:322` |
| REMOVE | - | Tap | Frees pool slot or disables fixed module and removes from band | `shell-mpc/ui/fx_slot_editor.h:317` |
| BAND TILE | - | Tap | Opens band processing chain into chain bar | `shell-mpc/ui/fx_band_board.h:54` |
| BAND CARD | - | Tap | Selects band effect and opens band chain into chain bar | `shell-mpc/ui/fx_band_board.h:1033` |
| BAND POWER | - | Tap power icon | Toggles power of effect sitting in splitter band | `shell-mpc/ui/fx_band_board.h:1035` |
| BAND REORDER | - | Drag horizontally | Moves effect position inside band processing order | `shell-mpc/ui/fx_band_board.h:1041` |
| BAND + ADD | - | Tap | Opens band chain and opens type list popup for free modules | `shell-mpc/ui/fx_band_board.h:1024` |
| TYPE ITEM | - | Tap | Selects effect type to instantiate into chain | `shell-mpc/overlay/fx_ui.h:1072` |
| PARAM CHOICE | - | Tap | Selects parameter option from dropdown list | `shell-mpc/overlay/fx_ui.h:1075` |
| LIST BACKDROP | - | Tap outside list | Closes list popup without applying selection | `shell-mpc/overlay/fx_ui.h:1125` |
| RATE | `chorus.rate` | Turn knob / Q-Link | [Chorus] Modulation rate in Hz (active when sync is Free) | `shell-mpc/ui/fx_layout.h:110` |
| DIVISION | `chorus.division` | Tap stepper / open list / Q-Link | [Chorus] Modulation tempo division (active when sync is Tempo) | `shell-mpc/ui/fx_layout.h:110` |
| SYNC | `chorus.sync` | Tap option / turn / Q-Link | [Chorus] Modulation sync mode: Free, Tempo, Dotted, Triplet | `shell-mpc/ui/fx_layout.h:110` |
| DEPTH | `chorus.depth` | Turn knob / Q-Link | [Chorus] Modulation depth percent | `shell-mpc/ui/fx_layout.h:110` |
| FEEDBACK | `chorus.feedback` | Turn knob / Q-Link | [Chorus] Feedback amount percent | `shell-mpc/ui/fx_layout.h:110` |
| VOICES | `chorus.voices` | Tap +/- / drag / Q-Link | [Chorus] Number of chorus delay voices (1 to 8) | `shell-mpc/ui/fx_layout.h:111` |
| DELAY 1 | `chorus.delay1` | Turn knob / Q-Link | [Chorus] Delay time for voice group 1 | `shell-mpc/ui/fx_layout.h:111` |
| DELAY 2 | `chorus.delay2` | Turn knob / Q-Link | [Chorus] Delay time for voice group 2 | `shell-mpc/ui/fx_layout.h:111` |
| FILTER CUTOFF | `chorus.filter_cutoff` | Turn knob / Q-Link | [Chorus] Chorus wet path filter cutoff frequency | `shell-mpc/ui/fx_layout.h:112` |
| FILTER SPREAD | `chorus.filter_spread` | Turn knob / Q-Link | [Chorus] Chorus wet path filter spread between voices | `shell-mpc/ui/fx_layout.h:112` |
| MIX | `chorus.mix` | Turn knob / Q-Link | [Chorus] Dry/wet mix percent | `shell-mpc/ui/fx_layout.h:112` |
| LEVEL | `chorus.level` | Turn knob / Q-Link | [Chorus] Output level in dB | `shell-mpc/ui/fx_layout.h:112` |
| MIX | `compressor.mix` | Turn knob / Q-Link | [Compressor] Dry/wet mix percent | `shell-mpc/ui/fx_layout.h:115` |
| ATTACK | `compressor.attack` | Turn knob / Q-Link | [Compressor] Attack time in milliseconds | `shell-mpc/ui/fx_layout.h:115` |
| RELEASE | `compressor.release` | Turn knob / Q-Link | [Compressor] Release time in milliseconds | `shell-mpc/ui/fx_layout.h:115` |
| BANDS | `compressor.bands` | Tap option / turn / Q-Link | [Compressor] Band split mode: Multiband, Low Band, High Band, Single Band | `shell-mpc/ui/fx_layout.h:115` |
| MODE | `compressor.limit` | Tap option / turn / Q-Link | [Compressor] Compressor mode / algorithm (Compress, Limit) | `shell-mpc/ui/fx_layout.h:116` |
| LIMITER LATENCY COMP | `compressor.limit_latency_comp` | Tap toggle / Q-Link | [Compressor] Limiter lookahead latency compensation (live when Limit mode active) | `shell-mpc/ui/fx_layout.h:116` |
| LOW CROSSOVER | `compressor.crossover_low` | Turn knob / Q-Link | [Compressor] Low/mid band crossover frequency (live in Multiband or Low Band modes) | `shell-mpc/ui/fx_layout.h:116` |
| HIGH CROSSOVER | `compressor.crossover_high` | Turn knob / Q-Link | [Compressor] Mid/high band crossover frequency (live in Multiband or High Band modes) | `shell-mpc/ui/fx_layout.h:116` |
| LOW LOWER THRESHOLD | `compressor.low.lower_threshold` | Turn knob / Q-Link | [Compressor] Low band expansion lower threshold in dB (live when low band active) | `shell-mpc/ui/fx_layout.h:118` |
| LOW UPPER THRESHOLD | `compressor.low.upper_threshold` | Turn knob / Q-Link | [Compressor] Low band compression upper threshold in dB (live when low band active) | `shell-mpc/ui/fx_layout.h:118` |
| LOW LOWER RATIO | `compressor.low.lower_ratio` | Turn knob / Q-Link | [Compressor] Low band downward ratio percent (live when low band active) | `shell-mpc/ui/fx_layout.h:118` |
| LOW UPPER RATIO | `compressor.low.upper_ratio` | Turn knob / Q-Link | [Compressor] Low band upward compression ratio percent (live when low band active) | `shell-mpc/ui/fx_layout.h:118` |
| MID LOWER THRESHOLD | `compressor.mid.lower_threshold` | Turn knob / Q-Link | [Compressor] Mid band expansion lower threshold in dB | `shell-mpc/ui/fx_layout.h:120` |
| MID UPPER THRESHOLD | `compressor.mid.upper_threshold` | Turn knob / Q-Link | [Compressor] Mid band compression upper threshold in dB | `shell-mpc/ui/fx_layout.h:120` |
| MID LOWER RATIO | `compressor.mid.lower_ratio` | Turn knob / Q-Link | [Compressor] Mid band downward ratio percent | `shell-mpc/ui/fx_layout.h:120` |
| MID UPPER RATIO | `compressor.mid.upper_ratio` | Turn knob / Q-Link | [Compressor] Mid band upward compression ratio percent | `shell-mpc/ui/fx_layout.h:120` |
| HIGH LOWER THRESHOLD | `compressor.high.lower_threshold` | Turn knob / Q-Link | [Compressor] High band expansion lower threshold in dB (live when high band active) | `shell-mpc/ui/fx_layout.h:121` |
| HIGH UPPER THRESHOLD | `compressor.high.upper_threshold` | Turn knob / Q-Link | [Compressor] High band compression upper threshold in dB (live when high band active) | `shell-mpc/ui/fx_layout.h:121` |
| HIGH LOWER RATIO | `compressor.high.lower_ratio` | Turn knob / Q-Link | [Compressor] High band downward ratio percent (live when high band active) | `shell-mpc/ui/fx_layout.h:121` |
| HIGH UPPER RATIO | `compressor.high.upper_ratio` | Turn knob / Q-Link | [Compressor] High band upward compression ratio percent (live when high band active) | `shell-mpc/ui/fx_layout.h:121` |
| LOW GAIN | `compressor.low.gain` | Turn knob / Q-Link | [Compressor] Low band make-up gain in dB (live when low band active) | `shell-mpc/ui/fx_layout.h:123` |
| MID GAIN | `compressor.mid.gain` | Turn knob / Q-Link | [Compressor] Mid band make-up gain in dB | `shell-mpc/ui/fx_layout.h:123` |
| HIGH GAIN | `compressor.high.gain` | Turn knob / Q-Link | [Compressor] High band make-up gain in dB (live when high band active) | `shell-mpc/ui/fx_layout.h:123` |
| LEVEL | `compressor.level` | Turn knob / Q-Link | [Compressor] Output level in dB | `shell-mpc/ui/fx_layout.h:123` |
| SYNC | `delay.sync` | Tap option / turn / Q-Link | [Delay] Delay sync mode: Free, Tempo, Dotted, Triplet | `shell-mpc/ui/fx_layout.h:126` |
| TIME | `delay.time` | Turn knob / Q-Link | [Delay] Delay tap 1 time in milliseconds (live when sync is Free) | `shell-mpc/ui/fx_layout.h:126` |
| DIVISION | `delay.division` | Tap stepper / open list / Q-Link | [Delay] Delay tap 1 tempo division (live when sync is Tempo) | `shell-mpc/ui/fx_layout.h:126` |
| TIME 2 | `delay.time2` | Turn knob / Q-Link | [Delay] Delay tap 2 time in milliseconds (live when sync is Free) | `shell-mpc/ui/fx_layout.h:126` |
| DIVISION 2 | `delay.division2` | Tap stepper / open list / Q-Link | [Delay] Delay tap 2 tempo division (live when sync is Tempo) | `shell-mpc/ui/fx_layout.h:126` |
| STYLE | `delay.style` | Tap option / turn / Q-Link | [Delay] Delay routing style: Mono, Stereo, Ping Pong, Mid Ping Pong, Tap Delay | `shell-mpc/ui/fx_layout.h:126` |
| FEEDBACK | `delay.feedback` | Turn knob / Q-Link | [Delay] Delay feedback amount percent | `shell-mpc/ui/fx_layout.h:128` |
| MIX | `delay.mix` | Turn knob / Q-Link | [Delay] Dry/wet mix percent | `shell-mpc/ui/fx_layout.h:128` |
| LEVEL | `delay.level` | Turn knob / Q-Link | [Delay] Output level in dB | `shell-mpc/ui/fx_layout.h:128` |
| HIGH QUALITY | `delay.hq` | Tap toggle / Q-Link | [Delay] High-quality interpolation toggle | `shell-mpc/ui/fx_layout.h:128` |
| FILTER CUTOFF | `delay.filter_cutoff` | Turn knob / Q-Link | [Delay] Feedback damping filter cutoff frequency | `shell-mpc/ui/fx_layout.h:129` |
| FILTER SPREAD | `delay.filter_spread` | Turn knob / Q-Link | [Delay] Feedback filter stereo spread | `shell-mpc/ui/fx_layout.h:129` |
| OFFSET | `delay.offset` | Turn knob / Q-Link | [Delay] Time offset between repeats | `shell-mpc/ui/fx_layout.h:129` |
| OFFSET LEFT | `delay.offset_left` | Turn knob / Q-Link | [Delay] Time offset on left channel | `shell-mpc/ui/fx_layout.h:129` |
| TYPE | `distortion.type` | Tap stepper / open list / Q-Link | [Distortion] Waveshaper distortion type / curve | `shell-mpc/ui/fx_layout.h:132` |
| DRIVE | `distortion.drive` | Turn knob / Q-Link | [Distortion] Distortion drive gain in dB | `shell-mpc/ui/fx_layout.h:132` |
| MIX | `distortion.mix` | Turn knob / Q-Link | [Distortion] Dry/wet mix percent | `shell-mpc/ui/fx_layout.h:132` |
| LEVEL | `distortion.level` | Turn knob / Q-Link | [Distortion] Output level in dB | `shell-mpc/ui/fx_layout.h:132` |
| FILTER ORDER | `distortion.filter_order` | Tap option / turn / Q-Link | [Distortion] Filter routing order: Off, Post, Pre | `shell-mpc/ui/fx_layout.h:133` |
| FILTER CUTOFF | `distortion.filter_cutoff` | Turn knob / Q-Link | [Distortion] Filter cutoff frequency (live when filter active) | `shell-mpc/ui/fx_layout.h:133` |
| FILTER RESONANCE | `distortion.filter_resonance` | Turn knob / Q-Link | [Distortion] Filter resonance percent (live when filter active) | `shell-mpc/ui/fx_layout.h:133` |
| FILTER BLEND | `distortion.filter_blend` | Turn knob / Q-Link | [Distortion] Filter blend / response shape (live when filter active) | `shell-mpc/ui/fx_layout.h:133` |
| FILTER KEY TRACK | `distortion.filter_keytrack` | Turn knob / Q-Link | [Distortion] Filter keyboard tracking amount (live when filter active) | `shell-mpc/ui/fx_layout.h:135` |
| LOW MODE | `eq.low.mode` | Tap option / turn / Q-Link | [EQ] Low band filter mode (Shelf, High-Pass) | `shell-mpc/ui/fx_layout.h:138` |
| LOW CUTOFF | `eq.low.cutoff` | Turn knob / Q-Link | [EQ] Low band cutoff frequency in Hz | `shell-mpc/ui/fx_layout.h:138` |
| LOW GAIN | `eq.low.gain` | Turn knob / Q-Link | [EQ] Low band boost/cut gain in dB | `shell-mpc/ui/fx_layout.h:138` |
| LOW RESONANCE | `eq.low.resonance` | Turn knob / Q-Link | [EQ] Low band Q / resonance percent | `shell-mpc/ui/fx_layout.h:138` |
| MID MODE | `eq.mid.mode` | Tap option / turn / Q-Link | [EQ] Mid band 1 filter mode (Peak, Notch) | `shell-mpc/ui/fx_layout.h:139` |
| MID CUTOFF | `eq.mid.cutoff` | Turn knob / Q-Link | [EQ] Mid band 1 center frequency in Hz | `shell-mpc/ui/fx_layout.h:139` |
| MID GAIN | `eq.mid.gain` | Turn knob / Q-Link | [EQ] Mid band 1 boost/cut gain in dB | `shell-mpc/ui/fx_layout.h:139` |
| MID RESONANCE | `eq.mid.resonance` | Turn knob / Q-Link | [EQ] Mid band 1 Q / resonance percent | `shell-mpc/ui/fx_layout.h:139` |
| HIGH MODE | `eq.high.mode` | Tap option / turn / Q-Link | [EQ] High band filter mode (Shelf, Low-Pass) | `shell-mpc/ui/fx_layout.h:140` |
| HIGH CUTOFF | `eq.high.cutoff` | Turn knob / Q-Link | [EQ] High band cutoff frequency in Hz | `shell-mpc/ui/fx_layout.h:140` |
| HIGH GAIN | `eq.high.gain` | Turn knob / Q-Link | [EQ] High band boost/cut gain in dB | `shell-mpc/ui/fx_layout.h:140` |
| HIGH RESONANCE | `eq.high.resonance` | Turn knob / Q-Link | [EQ] High band Q / resonance percent | `shell-mpc/ui/fx_layout.h:140` |
| MID 2 MODE | `eq.mid2.mode` | Tap option / turn / Q-Link | [EQ] Mid band 2 filter mode: Off, Peak, Notch | `shell-mpc/ui/fx_layout.h:141` |
| MID 2 CUTOFF | `eq.mid2.cutoff` | Turn knob / Q-Link | [EQ] Mid band 2 center frequency in Hz (live when mode != Off) | `shell-mpc/ui/fx_layout.h:141` |
| MID 2 GAIN | `eq.mid2.gain` | Turn knob / Q-Link | [EQ] Mid band 2 boost/cut gain in dB (live when mode != Off) | `shell-mpc/ui/fx_layout.h:141` |
| MID 2 RESONANCE | `eq.mid2.resonance` | Turn knob / Q-Link | [EQ] Mid band 2 Q / resonance percent (live when mode != Off) | `shell-mpc/ui/fx_layout.h:141` |
| LEVEL | `eq.level` | Turn knob / Q-Link | [EQ] Output level in dB | `shell-mpc/ui/fx_layout.h:143` |
| BAND SHAPE | `eq.law` | Tap option / turn / Q-Link | [EQ] EQ biquad section curve shaping law | `shell-mpc/ui/fx_layout.h:143` |
| MODEL | `fxfilter.model` | Tap stepper / open list / Q-Link | [FX Filter] Filter model selection | `shell-mpc/ui/fx_layout.h:146` |
| STYLE | `fxfilter.style` | Tap stepper / open list / Q-Link | [FX Filter] Filter style / slope selection | `shell-mpc/ui/fx_layout.h:146` |
| CUTOFF | `fxfilter.cutoff` | Turn knob / Q-Link | [FX Filter] Filter cutoff frequency in Hz | `shell-mpc/ui/fx_layout.h:146` |
| RESONANCE | `fxfilter.resonance` | Turn knob / Q-Link | [FX Filter] Filter resonance percent | `shell-mpc/ui/fx_layout.h:146` |
| COMB BLEND OFFSET | `fxfilter.comb_offset` | Turn knob / Q-Link | [FX Filter] Comb blend offset (live when model is Comb) | `shell-mpc/ui/fx_layout.h:147` |
| FORMANT X | `fxfilter.formant.x` | Turn knob / Q-Link | [FX Filter] Formant vowel X coordinate (live when model is Formant) | `shell-mpc/ui/fx_layout.h:147` |
| FORMANT Y | `fxfilter.formant.y` | Turn knob / Q-Link | [FX Filter] Formant vowel Y coordinate (live when model is Formant) | `shell-mpc/ui/fx_layout.h:147` |
| FORMANT RESONANCE | `fxfilter.formant.resonance` | Turn knob / Q-Link | [FX Filter] Formant resonance percent (live when model is Formant) | `shell-mpc/ui/fx_layout.h:147` |
| FORMANT SPREAD | `fxfilter.formant.spread` | Turn knob / Q-Link | [FX Filter] Formant vowel spread (live when model is Formant) | `shell-mpc/ui/fx_layout.h:147` |
| DRIVE | `fxfilter.drive` | Turn knob / Q-Link | [FX Filter] Filter saturation drive in dB | `shell-mpc/ui/fx_layout.h:149` |
| BLEND | `fxfilter.blend` | Turn knob / Q-Link | [FX Filter] Filter slope / topology blend | `shell-mpc/ui/fx_layout.h:149` |
| KEY TRACKING | `fxfilter.keytrack` | Turn knob / Q-Link | [FX Filter] Filter keyboard tracking amount | `shell-mpc/ui/fx_layout.h:149` |
| CLEAN MODE | `fxfilter.clean_mode` | Tap toggle / Q-Link | [FX Filter] Clean / linear filter processing toggle | `shell-mpc/ui/fx_layout.h:149` |
| VAR | `fxfilter.var` | Turn knob / Q-Link | [FX Filter] Filter variation / character parameter | `shell-mpc/ui/fx_layout.h:150` |
| PAN | `fxfilter.pan` | Turn knob / Q-Link | [FX Filter] Stereo pan offset | `shell-mpc/ui/fx_layout.h:150` |
| MIX | `fxfilter.mix` | Turn knob / Q-Link | [FX Filter] Dry/wet mix percent | `shell-mpc/ui/fx_layout.h:150` |
| LEVEL | `fxfilter.level` | Turn knob / Q-Link | [FX Filter] Output level in dB | `shell-mpc/ui/fx_layout.h:150` |
| FORMANT TRANSPOSE | `fxfilter.formant.transpose` | Turn knob / Q-Link | [FX Filter] Formant transpose pitch offset in semitones (live when model is Formant) | `shell-mpc/ui/fx_layout.h:151` |
| RATE | `flanger.rate` | Turn knob / Q-Link | [Flanger] LFO modulation rate in Hz (live when sync is Free) | `shell-mpc/ui/fx_layout.h:154` |
| DIVISION | `flanger.division` | Tap stepper / open list / Q-Link | [Flanger] LFO modulation tempo division (live when sync is Tempo) | `shell-mpc/ui/fx_layout.h:154` |
| SYNC | `flanger.sync` | Tap option / turn / Q-Link | [Flanger] Modulation sync mode: Free, Tempo, Dotted, Triplet | `shell-mpc/ui/fx_layout.h:154` |
| DEPTH | `flanger.depth` | Turn knob / Q-Link | [Flanger] Modulation depth percent | `shell-mpc/ui/fx_layout.h:154` |
| CENTER | `flanger.center` | Turn knob / Q-Link | [Flanger] Flanger delay center offset | `shell-mpc/ui/fx_layout.h:154` |
| FEEDBACK | `flanger.feedback` | Turn knob / Q-Link | [Flanger] Flanger feedback amount percent | `shell-mpc/ui/fx_layout.h:155` |
| PHASE OFFSET | `flanger.phase_offset` | Turn knob / Q-Link | [Flanger] Stereo LFO phase offset | `shell-mpc/ui/fx_layout.h:155` |
| MIX | `flanger.mix` | Turn knob / Q-Link | [Flanger] Dry/wet mix percent | `shell-mpc/ui/fx_layout.h:155` |
| LEVEL | `flanger.level` | Turn knob / Q-Link | [Flanger] Output level in dB | `shell-mpc/ui/fx_layout.h:155` |
| RATE | `phaser.rate` | Turn knob / Q-Link | [Phaser] LFO modulation rate in Hz (live when sync is Free) | `shell-mpc/ui/fx_layout.h:158` |
| DIVISION | `phaser.division` | Tap stepper / open list / Q-Link | [Phaser] LFO modulation tempo division (live when sync is Tempo) | `shell-mpc/ui/fx_layout.h:158` |
| SYNC | `phaser.sync` | Tap option / turn / Q-Link | [Phaser] Modulation sync mode: Free, Tempo, Dotted, Triplet | `shell-mpc/ui/fx_layout.h:158` |
| DEPTH | `phaser.depth` | Turn knob / Q-Link | [Phaser] Modulation depth stage 1 percent | `shell-mpc/ui/fx_layout.h:158` |
| DEPTH 2 | `phaser.depth2` | Turn knob / Q-Link | [Phaser] Modulation depth stage 2 percent | `shell-mpc/ui/fx_layout.h:158` |
| CENTER | `phaser.center` | Turn knob / Q-Link | [Phaser] All-pass filter notch center frequency | `shell-mpc/ui/fx_layout.h:159` |
| BLEND | `phaser.blend` | Turn knob / Q-Link | [Phaser] Poles / notch blend ratio | `shell-mpc/ui/fx_layout.h:159` |
| FEEDBACK | `phaser.feedback` | Turn knob / Q-Link | [Phaser] Phaser feedback amount percent | `shell-mpc/ui/fx_layout.h:159` |
| PHASE OFFSET | `phaser.phase_offset` | Turn knob / Q-Link | [Phaser] Stereo LFO phase offset | `shell-mpc/ui/fx_layout.h:159` |
| MIX | `phaser.mix` | Turn knob / Q-Link | [Phaser] Dry/wet mix percent | `shell-mpc/ui/fx_layout.h:160` |
| LEVEL | `phaser.level` | Turn knob / Q-Link | [Phaser] Output level in dB | `shell-mpc/ui/fx_layout.h:160` |
| MIX | `reverb.mix` | Turn knob / Q-Link | [Reverb] Dry/wet mix percent | `shell-mpc/ui/fx_layout.h:163` |
| SIZE | `reverb.size` | Turn knob / Q-Link | [Reverb] Reverb space size percent | `shell-mpc/ui/fx_layout.h:163` |
| DECAY | `reverb.decay` | Turn knob / Q-Link | [Reverb] Reverb decay time | `shell-mpc/ui/fx_layout.h:163` |
| PRE-DELAY | `reverb.predelay` | Turn knob / Q-Link | [Reverb] Pre-delay time before reverb onset | `shell-mpc/ui/fx_layout.h:163` |
| ALGORITHM | `reverb.type` | Tap option / turn / Q-Link | [Reverb] Reverb algorithm type selection | `shell-mpc/ui/fx_layout.h:164` |
| WIDTH | `reverb.width` | Turn knob / Q-Link | [Reverb] Stereo width percent | `shell-mpc/ui/fx_layout.h:164` |
| ER SIZE | `reverb.er_size` | Turn knob / Q-Link | [Reverb] Early reflections room size | `shell-mpc/ui/fx_layout.h:164` |
| ROOM | `reverb.room` | Tap option / turn / Q-Link | [Reverb] Room acoustic character (live when algorithm is Room) | `shell-mpc/ui/fx_layout.h:164` |
| DIFFUSION A | `reverb.diff_a` | Turn knob / Q-Link | [Reverb] Early diffusion coefficient | `shell-mpc/ui/fx_layout.h:165` |
| DIFFUSION B | `reverb.diff_b` | Turn knob / Q-Link | [Reverb] Late diffusion coefficient | `shell-mpc/ui/fx_layout.h:165` |
| LEVEL | `reverb.level` | Turn knob / Q-Link | [Reverb] Output level in dB | `shell-mpc/ui/fx_layout.h:165` |
| FEEDBACK | `reverb.feedback` | Turn knob / Q-Link | [Reverb] Feedback decay amount (live when algorithm is Room or Plate) | `shell-mpc/ui/fx_layout.h:165` |
| CHORUS AMOUNT | `reverb.chorus_amount` | Turn knob / Q-Link | [Reverb] Internal tank chorus modulation depth | `shell-mpc/ui/fx_layout.h:166` |
| CHORUS RATE | `reverb.chorus_rate` | Turn knob / Q-Link | [Reverb] Internal tank chorus modulation rate | `shell-mpc/ui/fx_layout.h:166` |
| PRE LOW CUT | `reverb.pre_low_cut` | Turn knob / Q-Link | [Reverb] Input pre-filter low cut frequency | `shell-mpc/ui/fx_layout.h:166` |
| PRE HIGH CUT | `reverb.pre_high_cut` | Turn knob / Q-Link | [Reverb] Input pre-filter high cut frequency | `shell-mpc/ui/fx_layout.h:166` |
| LOW SHELF CUTOFF | `reverb.low_shelf_cutoff` | Turn knob / Q-Link | [Reverb] Low shelf damping cutoff frequency | `shell-mpc/ui/fx_layout.h:167` |
| LOW SHELF GAIN | `reverb.low_shelf_gain` | Turn knob / Q-Link | [Reverb] Low shelf damping gain in dB | `shell-mpc/ui/fx_layout.h:167` |
| HIGH SHELF CUTOFF | `reverb.high_shelf_cutoff` | Turn knob / Q-Link | [Reverb] High shelf damping cutoff frequency | `shell-mpc/ui/fx_layout.h:167` |
| HIGH SHELF GAIN | `reverb.high_shelf_gain` | Turn knob / Q-Link | [Reverb] High shelf damping gain in dB | `shell-mpc/ui/fx_layout.h:167` |
| FORCE ON | `reverb.force_enable` | Tap toggle / Q-Link | [Reverb] Force reverb active even when CPU tiering limits are met | `shell-mpc/ui/fx_layout.h:168` |
| RATE | `hyperdim.rate` | Turn knob / Q-Link | [Hyper/Dimension] Hyper unison chorus rate | `shell-mpc/ui/fx_layout.h:171` |
| UNISON | `hyperdim.unison` | Tap +/- / drag / Q-Link | [Hyper/Dimension] Hyper unison voice count | `shell-mpc/ui/fx_layout.h:171` |
| DETUNE | `hyperdim.detune` | Turn knob / Q-Link | [Hyper/Dimension] Hyper unison detune amount | `shell-mpc/ui/fx_layout.h:171` |
| RETRIGGER | `hyperdim.retrig` | Tap toggle / Q-Link | [Hyper/Dimension] Hyper unison phase retrigger toggle | `shell-mpc/ui/fx_layout.h:171` |
| DIMENSION SIZE | `hyperdim.dim_size` | Turn knob / Q-Link | [Hyper/Dimension] Dimension spatial room size | `shell-mpc/ui/fx_layout.h:172` |
| DIMENSION MIX | `hyperdim.dim_mix` | Turn knob / Q-Link | [Hyper/Dimension] Dimension expander mix percent | `shell-mpc/ui/fx_layout.h:172` |
| MIX | `hyperdim.mix` | Turn knob / Q-Link | [Hyper/Dimension] Overall dry/wet mix percent | `shell-mpc/ui/fx_layout.h:172` |
| LEVEL | `hyperdim.level` | Turn knob / Q-Link | [Hyper/Dimension] Output level in dB | `shell-mpc/ui/fx_layout.h:172` |
| POLARITY LEFT | `utility.polarity_l` | Tap toggle / Q-Link | [Utility] Left channel phase invert toggle | `shell-mpc/ui/fx_layout.h:175` |
| POLARITY RIGHT | `utility.polarity_r` | Tap toggle / Q-Link | [Utility] Right channel phase invert toggle | `shell-mpc/ui/fx_layout.h:175` |
| WIDTH | `utility.width` | Turn knob / Q-Link | [Utility] Stereo width percent (0 = mono, 100 = stereo, >100 = widened) | `shell-mpc/ui/fx_layout.h:175` |
| PAN | `utility.pan` | Turn knob / Q-Link | [Utility] Stereo balance / pan | `shell-mpc/ui/fx_layout.h:175` |
| LOW PASS | `utility.lpf` | Turn knob / Q-Link | [Utility] Utility low-pass filter cutoff | `shell-mpc/ui/fx_layout.h:176` |
| HIGH PASS | `utility.hpf` | Turn knob / Q-Link | [Utility] Utility high-pass filter cutoff | `shell-mpc/ui/fx_layout.h:176` |
| MONO BASS | `utility.mono_bass` | Tap toggle / Q-Link | [Utility] Mono bass processing toggle | `shell-mpc/ui/fx_layout.h:176` |
| MONO BASS FREQUENCY | `utility.mono_freq` | Turn knob / Q-Link | [Utility] Mono bass crossover frequency (live when mono bass is On) | `shell-mpc/ui/fx_layout.h:176` |
| MIX | `utility.mix` | Turn knob / Q-Link | [Utility] Dry/wet mix percent | `shell-mpc/ui/fx_layout.h:177` |
| LEVEL | `utility.level` | Turn knob / Q-Link | [Utility] Output gain in dB | `shell-mpc/ui/fx_layout.h:177` |
| SHIFT | `bode.shift` | Turn knob / Q-Link | [Bode] Frequency shift offset amount | `shell-mpc/ui/fx_layout.h:180` |
| RANGE | `bode.range` | Turn knob / Q-Link | [Bode] Frequency shift range multiplier | `shell-mpc/ui/fx_layout.h:180` |
| DIRECTION | `bode.dir` | Turn knob / Q-Link | [Bode] Shift direction (Up, Down, Both) | `shell-mpc/ui/fx_layout.h:180` |
| BALANCE | `bode.balance` | Turn knob / Q-Link | [Bode] Up/down frequency balance | `shell-mpc/ui/fx_layout.h:180` |
| DELAY | `bode.delay` | Turn knob / Q-Link | [Bode] Frequency shifter feedback delay time in ms (live when sync is Free) | `shell-mpc/ui/fx_layout.h:181` |
| DIVISION | `bode.division` | Tap stepper / open list / Q-Link | [Bode] Frequency shifter feedback delay tempo division (live when sync is Tempo) | `shell-mpc/ui/fx_layout.h:181` |
| SYNC | `bode.sync` | Tap option / turn / Q-Link | [Bode] Delay sync mode: Free, Tempo, Dotted, Triplet | `shell-mpc/ui/fx_layout.h:181` |
| FEEDBACK | `bode.feed` | Turn knob / Q-Link | [Bode] Feedback amount percent | `shell-mpc/ui/fx_layout.h:181` |
| BLUR | `bode.blur` | Turn knob / Q-Link | [Bode] Diffusion / frequency blur percent | `shell-mpc/ui/fx_layout.h:181` |
| WIDTH | `bode.width` | Turn knob / Q-Link | [Bode] Stereo frequency shift spread | `shell-mpc/ui/fx_layout.h:182` |
| MONO INPUT | `bode.mono_input` | Tap toggle / Q-Link | [Bode] Mono input summing toggle | `shell-mpc/ui/fx_layout.h:182` |
| RETRIGGER | `bode.retrig` | Tap toggle / Q-Link | [Bode] LFO modulation retrigger toggle | `shell-mpc/ui/fx_layout.h:182` |
| MIX | `bode.mix` | Turn knob / Q-Link | [Bode] Dry/wet mix percent | `shell-mpc/ui/fx_layout.h:183` |
| LEVEL | `bode.level` | Turn knob / Q-Link | [Bode] Output level in dB | `shell-mpc/ui/fx_layout.h:183` |
| IMPULSE | `convolve.ir` | Tap option / turn / Q-Link | [Convolve] Impulse response selection | `shell-mpc/ui/fx_layout.h:186` |
| SIZE | `convolve.size` | Turn knob / Q-Link | [Convolve] Impulse response duration scaling | `shell-mpc/ui/fx_layout.h:186` |
| TONE | `convolve.tone` | Turn knob / Q-Link | [Convolve] Impulse response spectral tilt / tone | `shell-mpc/ui/fx_layout.h:186` |
| MINIMUM PHASE | `convolve.phi_min` | Tap toggle / Q-Link | [Convolve] Minimum phase reconstruction toggle | `shell-mpc/ui/fx_layout.h:186` |
| PRE-DELAY | `convolve.predelay` | Turn knob / Q-Link | [Convolve] Convolver pre-delay in ms (live when sync is Free) | `shell-mpc/ui/fx_layout.h:187` |
| PRE-DELAY | `convolve.division` | Tap stepper / open list / Q-Link | [Convolve] Convolver pre-delay tempo division (live when sync is Tempo) | `shell-mpc/ui/fx_layout.h:187` |
| SYNC | `convolve.sync` | Tap option / turn / Q-Link | [Convolve] Pre-delay sync mode: Free, Tempo, Dotted, Triplet | `shell-mpc/ui/fx_layout.h:187` |
| ATTACK | `convolve.attack` | Turn knob / Q-Link | [Convolve] Impulse response envelope attack time | `shell-mpc/ui/fx_layout.h:187` |
| DECAY | `convolve.decay` | Turn knob / Q-Link | [Convolve] Impulse response envelope decay time | `shell-mpc/ui/fx_layout.h:187` |
| DAMPING | `convolve.damp` | Turn knob / Q-Link | [Convolve] High frequency damping amount | `shell-mpc/ui/fx_layout.h:188` |
| IR GAIN | `convolve.ir_gain` | Turn knob / Q-Link | [Convolve] Impulse response makeup gain in dB | `shell-mpc/ui/fx_layout.h:188` |
| SCALING | `convolve.law` | Tap option / turn / Q-Link | [Convolve] Impulse response size scaling law | `shell-mpc/ui/fx_layout.h:188` |
| MIX | `convolve.mix` | Turn knob / Q-Link | [Convolve] Dry/wet mix percent | `shell-mpc/ui/fx_layout.h:189` |
| LEVEL | `convolve.level` | Turn knob / Q-Link | [Convolve] Output level in dB | `shell-mpc/ui/fx_layout.h:189` |
| SPLIT FREQUENCY | `splitter_lh.freq` | Turn knob / Q-Link | [Splitter L/H] Crossover split frequency between Low and High bands | `shell-mpc/ui/fx_layout.h:192` |
| LOW BYPASS | `splitter_lh.low_bypass` | Tap toggle / Q-Link | [Splitter L/H] Low band bypass toggle | `shell-mpc/ui/fx_layout.h:192` |
| HIGH BYPASS | `splitter_lh.high_bypass` | Tap toggle / Q-Link | [Splitter L/H] High band bypass toggle | `shell-mpc/ui/fx_layout.h:192` |
| LEVEL | `splitter_lh.level` | Turn knob / Q-Link | [Splitter L/H] Output level in dB | `shell-mpc/ui/fx_layout.h:192` |
| LOW SPLIT | `splitter_lmh.freq_low` | Turn knob / Q-Link | [Splitter L/M/H] Crossover split frequency between Low and Mid bands | `shell-mpc/ui/fx_layout.h:194` |
| HIGH SPLIT | `splitter_lmh.freq_high` | Turn knob / Q-Link | [Splitter L/M/H] Crossover split frequency between Mid and High bands | `shell-mpc/ui/fx_layout.h:194` |
| LEVEL | `splitter_lmh.level` | Turn knob / Q-Link | [Splitter L/M/H] Output level in dB | `shell-mpc/ui/fx_layout.h:194` |
| LOW BYPASS | `splitter_lmh.low_bypass` | Tap toggle / Q-Link | [Splitter L/M/H] Low band bypass toggle | `shell-mpc/ui/fx_layout.h:195` |
| MID BYPASS | `splitter_lmh.mid_bypass` | Tap toggle / Q-Link | [Splitter L/M/H] Mid band bypass toggle | `shell-mpc/ui/fx_layout.h:195` |
| HIGH BYPASS | `splitter_lmh.high_bypass` | Tap toggle / Q-Link | [Splitter L/M/H] High band bypass toggle | `shell-mpc/ui/fx_layout.h:195` |
| MID BYPASS | `splitter_ms.mid_bypass` | Tap toggle / Q-Link | [Splitter M/S] Mid (center) channel bypass toggle | `shell-mpc/ui/fx_layout.h:197` |
| SIDE BYPASS | `splitter_ms.side_bypass` | Tap toggle / Q-Link | [Splitter M/S] Side (stereo difference) channel bypass toggle | `shell-mpc/ui/fx_layout.h:197` |
| LEVEL | `splitter_ms.level` | Turn knob / Q-Link | [Splitter M/S] Output level in dB | `shell-mpc/ui/fx_layout.h:197` |

## Opens

1. **Effect Type Selection List (`ListPopup`, `shell-mpc/overlay/fx_ui.h:858-880`):**
   - **Trigger:** Tapping the `+ ADD` card (`shell-mpc/ui/fx_chain_bar.h:101`) or spare faint add cards (`shell-mpc/ui/fx_chain_bar.h:376`) in the top chain bar, or tapping a band `+ ADD` card (`shell-mpc/ui/fx_band_board.h:1024`) in `FxBandBoard`.
   - **View:** Opens popup detail page `kPageList = 35` on the stack (`stack.openDetail(kPageList)`).
   - **Content:** Scrollable vertical list of effect type names. In Main/Bus chains, offers all 13 pool effect types plus the 3 splitters. Inside a band scope, offers only unassigned free modules of instance 0 (`shell-mpc/overlay/fx_ui.h:345-352`).
   - **Controls:**
     - List item: Gesture `Tap`, Effect: instantiates effect of selected type and selects it (`shell-mpc/overlay/fx_ui.h:1072`).
     - List backdrop: Gesture `Tap outside list`, Effect: dismisses popup without action (`shell-mpc/overlay/fx_ui.h:1125`).

2. **Parameter Choice Selection List (`ListPopup`, `shell-mpc/overlay/fx_ui.h:882-933`):**
   - **Trigger:** Tapping a stepper control with more than 8 choices or dynamic style choices (e.g. `fxfilter.model` or `fxfilter.style` when model has multiple styles) (`shell-mpc/overlay/fx_ui.h:1282`).
   - **View:** Opens popup detail page `kPageList = 35` on the stack (`stack.openDetail(kPageList)`).
   - **Content:** Vertical list of available choice names.
   - **Controls:**
     - Choice row: Gesture `Tap`, Effect: selects and writes parameter choice (`shell-mpc/overlay/fx_ui.h:1075`).
     - List backdrop: Gesture `Tap outside list`, Effect: dismisses popup without changing value (`shell-mpc/overlay/fx_ui.h:1125`).

3. **Splitter Band Scope Detail View (`enterBand`, `shell-mpc/overlay/fx_ui.h:986-994`):**
   - **Trigger:** Tapping a band tile `FxBandTile` (`shell-mpc/ui/fx_band_board.h:54`), tapping a band slot card (`shell-mpc/ui/fx_band_board.h:1033`), or tapping a band `+ ADD` card (`shell-mpc/ui/fx_band_board.h:1024`).
   - **View:** Replaces the main chain bar with the processing chain of the selected band (racks 3..9). Breadcrumb header updates to show full path (e.g. "MAIN CHAIN > SPLITTER L/M/H > MID BAND").
   - **Controls:**
     - `BACK` chevron card (`shell-mpc/ui/fx_chain_bar.h:104`): Gesture `Tap`, Effect: exits band and returns to parent splitter view (`leaveBand`, `shell-mpc/overlay/fx_ui.h:996-1007`).
     - Band rack slot cards, plusCard, and slot editor for effects inside that band.

## Q-Link groups

Published to the MPC hardware through `QSlots::publish` (`shell-mpc/overlay/fx_ui.h:814-847`):

1. **When List Popup is Open:**
   - Publishes an empty group labeled "List" (`shell-mpc/overlay/fx_ui.h:822-823`), muting hardware knob assignments so encoders do not change obscured background parameters.

2. **When an Effect is Selected on Page (`shell-mpc/overlay/fx_ui.h:825-844`):**
   - The rows (blocks) of the effect layout table (`shell-mpc/ui/fx_layout.h:63-67`) form the Q-Link groups (`shell-mpc/ui/fx_slot_editor.h:52`).
   - The active row on screen is mapped to hardware Q-Links 1 through 4.
   - Additional visible rows of the current page wrap across hardware Q-Links 5 through 16 (`QRouter::blockOfGroup`, `shell-mpc/overlay/fx_ui.h:842`).
   - Hardware group title (`qGroupName`): Displays the active row block title (e.g. "MODULATION", "VOICES AND DELAY", "LOW BAND", "GAINS").
   - A touch on any control within a row immediately switches that row to be the primary active Q-Link group (`shell-mpc/ui/fx_slot_editor.h:271-279`).
   - When a parameter condition (`Live`) is false, the cell is dim and its Q-Link slot target is set to -1 (`shell-mpc/overlay/fx_ui.h:838`).
   - Physical encoder rotation delegates to `slotEditor.turnQ(knobIndex, delta)` (`shell-mpc/ui/fx_slot_editor.h:283`), stepping the bound parameter.

3. **When Chain is Empty:**
   - Publishes 0 active Q-Link slots (`shell-mpc/overlay/fx_ui.h:845`).

## Live parts

1. **Chain Structure Tracking (`structureSignature`, `shell-mpc/overlay/fx_ui.h:239-269`):**
   - Evaluates a 32-bit hash signature on every update over `fx.order.1` through `fx.order.16`, pool instance parameters (`type`, `on`, `rack`, `order`), fixed module power/rack states, and splitter parameters (`splitter_lh.on`, `splitter_lh.rack`, `splitter_lmh.on`, `splitter_lmh.rack`, `splitter_ms.on`, `splitter_ms.rack`). Rebuilds slot cards whenever ordering or routing changes.

2. **Dynamic Row Liveness and Reflow (`reflow`, `shell-mpc/ui/fx_slot_editor.h:481-517`):**
   - Parameters with conditional liveness (`Live`, `shell-mpc/ui/fx_layout.h:35-46`) re-evaluate on every frame (`setState`, `shell-mpc/ui/fx_slot_editor.h:203-219`):
     - Sync pairing: `chorus.rate` vs `chorus.division`, `delay.time` vs `delay.division`, `flanger.rate` vs `flanger.division`, `phaser.rate` vs `phaser.division`, `bode.delay` vs `bode.division`, and `convolve.predelay` vs `convolve.division` toggle visibility based on their respective `sync` mode.
     - Compressor bands: Low and high threshold/ratio rows hide or dim when single band mode is chosen via `compressor.bands`.
     - Filter controls: In Distortion, filter controls hide when `distortion.filter_order` is Off. In FX Filter, Comb offset and Formant controls toggle based on `fxfilter.model`.
     - Inactive controls trailing a row collapse to save vertical space; inactive controls preceding a live control remain visible as dimmed non-interactive placeholders.

3. **Real-time Graphical Visualizations (`FxDisplay`, `shell-mpc/ui/fx_display.h:145-458`, `shell-mpc/overlay/fx_ui.h:693-810`):**
   - **EQ (`shell-mpc/ui/fx_display.h:204-230`):** Frequency response curve plotted across 192 logarithmic points (20 Hz to 20 kHz) by evaluating Audio EQ Cookbook biquad sections; band center indicator dots animate dynamically with frequency and gain adjustments.
   - **Compressor (`shell-mpc/ui/fx_display.h:233-340`):** Three transfer curves (LOW, MID, HIGH) with knee points at threshold values; the active band curve is highlighted.
   - **Delay (`shell-mpc/ui/fx_display.h:342-392`):** Discrete stem markers plotted on a millisecond time axis representing echo taps and repeat decays based on tempo/division and feedback.
   - **FX Filter (`shell-mpc/overlay/fx_ui.h:770-810`):** Real-time frequency response evaluated via `FilterResponse` (256 points) when parameters update, with moving vertical cutoff marker.
   - **Distortion (`shell-mpc/ui/fx_display.h:417-457`):** Waveshaper curve plotted across input/output transfer coordinates; dynamic live indicator dot animates on the curve driven by `live_->stripPeak` meter readouts (`shell-mpc/overlay/fx_ui.h:761-765`).

4. **Splitter Live Info (`pullBands`, `shell-mpc/overlay/fx_ui.h:590-664`):**
   - Band frequency crossover boundaries are dynamically formatted ("below 500 Hz", "500 Hz to 3.0 kHz", "above 3.0 kHz"), along with bypass status and module count strings ("3 effects", "SPLITTER OFF").

5. **Missing Content Alert (`shell-mpc/overlay/fx_ui.h:449-460`):**
   - Displays live notice text (e.g. "content missing: <filename>") in the header if a Convolve impulse response is absent.

## Duplicates

- An exhaustive audit of parameter keys across all MPC overlay pages in `shell-mpc/overlay/` and `shell-mpc/ui/` confirms that no parameter key belonging to the FX system (`chorus.*`, `compressor.*`, `delay.*`, `distortion.*`, `eq.*`, `fxfilter.*`, `flanger.*`, `phaser.*`, `reverb.*`, `hyperdim.*`, `utility.*`, `bode.*`, `convolve.*`, `splitter_lh.*`, `splitter_lmh.*`, `splitter_ms.*`, `fx.order.*`) is duplicated on any other overlay page (such as `osc_ui.h`, `filter_ui.h`, `env_ui.h`, `lfo_ui.h`, `mod_ui.h`, `mixer_ui.h`, `master_ui.h`, `voice_ui.h`, `macro_ui.h`, or `play_view.h`).
- Shared choice label helpers in `shell-mpc/ui/choice_names.h:29-37` inspect enum values for `eq.law`, `convolve.law`, `reverb.type`, and `compressor.limit`, but these provide display formatting rather than duplicate controls.
- All effect routing and DSP parameters are exclusively managed by the FX overlay page.

## Questionable

1. **Redundant Faint Add Button Spares (`shell-mpc/ui/fx_chain_bar.h:321,334-338,408-418`):**
   - `FxChainBar` instantiates up to 16 `spare` cards that render faint `+` symbols filling whatever unused horizontal space remains across the chain bar. Every spare card triggers the exact same `kAdd` action (`shell-mpc/ui/fx_chain_bar.h:376`) as the dedicated `plusCard`. This creates multiple identical add touch targets in a row, which can confuse users expecting distinct slots or place holders.

2. **Single-Item Dedicated Q-Link Row for `reverb.force_enable` (`shell-mpc/ui/fx_layout.h:168`):**
   - `reverb.force_enable` is placed in its own 6th row block titled "TIER" with custom label "FORCE ON". Because each row block constitutes a 4-knob Q-Link group and advances page height, this single toggle consumes an entire page row while leaving cells 1, 2, and 3 blank. It forces Reverb into 6 rows (2 parameter pages), whereas grouping it into the "OUTPUT" or "ALGORITHM" row would eliminate a page split.

3. **Identical Alt Labels for Rate/Division Pairs in Convolve (`shell-mpc/ui/fx_layout.h:187`):**
   - In `convolve.predelay` and `convolve.division`, both controls are explicitly assigned the alt name "PRE-DELAY" (`rate("convolve.predelay", "convolve.division", "convolve.sync", "PRE-DELAY", "PRE-DELAY")`). Unlike Chorus, Delay, and Flanger which distinguish "RATE" vs "DIVISION" or "TIME" vs "DIVISION", Convolve shows "PRE-DELAY" regardless of whether the parameter is in milliseconds or tempo division, obscuring the active mode.

4. **Sparse 3-Control Splitter Rows (`shell-mpc/ui/fx_layout.h:194-195`):**
   - In `Splitter L/M/H`, Row 0 ("SPLIT") contains 3 controls (`splitter_lmh.freq_low`, `splitter_lmh.freq_high`, `splitter_lmh.level`), leaving cell 3 blank. Row 1 ("BYPASS") contains 3 bypass buttons (`splitter_lmh.low_bypass`, `splitter_lmh.mid_bypass`, `splitter_lmh.high_bypass`), leaving cell 3 blank again. This leaves 2 out of 8 hardware Q-Links unassigned across the two rows.

5. **Isolated Single-Control Row for Distortion Keytrack (`shell-mpc/ui/fx_layout.h:135`):**
   - Row 2 of Distortion ("FILTER KEY") holds only `distortion.filter_keytrack`, leaving 3 cells empty and expanding Distortion to 3 rows. Pairing or integrating key tracking into row 1 would allow Distortion to fit into 2 compact rows.
