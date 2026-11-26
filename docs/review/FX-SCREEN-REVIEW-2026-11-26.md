# FX screen review, 2026-11-26

Scope: every effect page of the FX screen (`shell-mpc/ui/fx_layout.h`, `fx_slot_editor.h`, `fx_display.h`, the native cells `shell-mpc/native_fx_cells.h`, the skin `scripts/mpc-skin-extras/20_fx.py`). Yardstick: the Distortion page redrawn earlier the same day (display left, controls right, `fxl::Layout::side`). Pictures: synthetic offline renders (`python3 scripts/render_ui_screen_fx_preview.py --out-dir <outside the repo>`, every effect and panel), not hardware photos; `docs/screens/fx.jpg` (the Distortion page) did not change. Device checks that remain: `TODO.md` ("FX side layouts on the MPC").

## Rules applied to every page

- **Side layout wherever there is a display.** The stacked pages gave the curve a strip of 113 to 128 px over the whole width and left the lower third empty. Every effect with a display (Compressor, Delay, Distortion, EQ, FX Filter) now has the display at the left (628 px wide, the whole height under the header or the tabs) and the cells at the right, two per line. A panel is a side page only when its visible rows fit (`FxSlotEditor::sideFits`: 412 px without tabs, 348 px under them); otherwise, and in the Mixer peek, it stays stacked. The native layouts of the side pages are named by the lines of the rows on the panel now, so a row that hides moves the ones below it up on the device too.
- **Stacked cells at most 104 px high** (`nativeui::fxStackedCellH`, contract `cell_max_h`). Pages of one or two rows made a switch a dark box of 190 px (Hyper/Dimension RETRIGGER, Utility, splitters).
- **Q-Link order** changes only where related controls were split across Q groups; every change is listed below. A row is still one Q group; no row has more than four members.
- **Displays model the engine**, and `ui_fx_editor_host` (`testDisplayModels`) compares them with the engine's own effects (EQ impulse response, compressor steady levels, delay impulse).

## Per effect

### Distortion
- Was: the yardstick (side layout, waveshaper curve). Kept unchanged.
- Fixed: the filter-off state (FILTER SHAPE row gone) now has its own native layout (`{208, 2, side, {2, 2}}`), so the device places the cells by the rows that are on screen.

### Compressor
- Was: three transfer plots of 375 x 50 px in a strip; the Limit mode drew the ratio slope although the limiter holds a ceiling; the mix and the threshold clamp were not in the curve.
- Fixed: side layout on MAIN / BANDS / UPWARD; one square plot (302 px) with the bands in their colours and each band's thresholds and ratios (or CEILING) at its left; `compOut` follows the engine's gain computer (upward and downward stages at once where the thresholds cross, Limit = ceiling, make-up, linear mix, clamp -120 .. +12 dB). Measured against the engine: within 0.36 dB; the axis is the peak level of a steady tone (the Compress envelope reads a 1 kHz sine 2.4 dB above its RMS at the default times).
- The BAND selector stands at the right of the BAND row (cell 4); the docked peek uses its stacked place (`band_stacked` in the contract).
- Q order unchanged.

### Delay
- Was: TIME = style, sync, time, FEEDBACK; the second time sat in "FILTER AND OUTPUT" next to cutoff and mix; FILTER SPREAD was on the second panel apart from FILTER CUTOFF; "OFFSET" (the right side's factor, `delay.offset`) and "OFFSET LEFT" read like a common offset and its exception. The display drew both styles' echoes from the left time only (ping pong alternating, the second time and the offsets ignored; Mono / Tap -> delay without the right side).
- Fixed: TIME = style, sync, time L, time R (Q 1..4); FEEDBACK AND MIX = feedback, filter cutoff, filter spread, mix (Q 5..8); the second panel is OUTPUT (level, HQ; OFFSET L, OFFSET R). Side layout on both panels. The echoes come from the engine's network of each style (`FxDisplay::delayEchoes`), the times scaled by the offsets; measured against the engine: every echo of the five styles in time, side and strength, and no other.
- Q order changed (documented here): Q 4 FEEDBACK -> TIME R, Q 5..8 now feedback / cutoff / spread / mix.

### EQ
- Was: a 113 px strip; MAIN cutoff / gain, TONE modes + resonance + the second mid band (three rows, so the curve was hidden on TONE). The curve used cookbook biquads with a Q mapping that is in neither of the engine's laws (0.5 x 20^r) and ignored the law. A band's GAIN stayed on screen in the high pass, notch and low pass modes, which do not read it.
- Fixed: panels MAIN / TONE / MID 2, all side pages, so the curve is on every panel. `bandDb` is the engine's response in both laws (state variable: exact analog section of the TPT filter; cookbook with the engine's Q); measured within 0.05 dB against the engine's impulse response. A GAIN is live only in its band's gain mode (shelf, peak); the dot of a band without gain stands on the curve.
- Q order unchanged (the rows are the same; the MID BAND 2 row has its own panel).

### FX Filter
- Was: a 128 px strip on MAIN; TONE without the curve.
- Fixed: side layout on MAIN and TONE. TONE of the Formant model has three rows (2 + 2 + 1 lines = 408 px) and does not fit under the tabs: it stays stacked without the curve (checked by `testSideLayout`).
- Q order unchanged. The response is the engine's filter on an impulse (unchanged).

### Chorus
- Was: VOICES, MIX, DELAY 1, DELAY 2 (the mix between the voice count and the voices' delays).
- Fixed: VOICES, DELAY 1, DELAY 2, MIX. Q 6..8 changed (delay 1, delay 2, mix); the mix stays in the second bank.

### Flanger
- Was: three rows, two of them with two cells (FEEDBACK, MIX / PHASE OFFSET, LEVEL).
- Fixed: two rows; FEEDBACK AND OUTPUT = feedback, phase offset, mix, level (Q 5..8). On the Key 61 two banks instead of three.

### Phaser
- Was: FEEDBACK, MIX / DEPTH 2, BLEND, PHASE OFFSET, LEVEL.
- Fixed: the same second block as the Flanger (feedback, phase offset, mix, level on Q 5..8), then SWEEP = depth 2, blend.

### Hyper/Dimension
- Was: three rows; DIMENSION SIZE in the HYPER row, DIMENSION MIX in another, RETRIGGER alone with LEVEL.
- Fixed: HYPER = rate, unison, detune, retrigger; DIMENSION AND OUTPUT = dimension size, dimension mix, mix, level.

### Bode, Convolve
- Was: two panels of two rows each (half of each page empty); Convolve's SYNC on the TAIL panel while the PRE-DELAY it switches was on MAIN.
- Fixed: one page of four rows each (16 Q-Links on the XL without a tab). Q order unchanged.

### Reverb, Utility, splitters
- Reviewed, grouping kept (Reverb: MAIN algorithm/size/decay/predelay/width/room/feedback/output, TONE filters and damping, MOD diffusion and chorus; Utility: stereo and filters, mono bass and mix, polarity and output). Only the stacked cell cap applies.

## Open (in `TODO.md`)

- Device check of every changed page and the changed Q order.
- The displays take no touch (the design's EQ nodes, thresholds, times).
- Effects without a display stay knob pages.
- The native number drawer (VOICES, UNISON) and switch cells need a look on the device.
