# LFO screen (`lfoScreen`, `lfoScreenTop`, `lfoScreenTop2`, `lfoScreenTop3`)

**Tabs:** LFO 1 .. LFO 10 (`lfo1` .. `lfo10`) and RND 1 .. RND 4 (`rnd1` .. `rnd4`); the view index is 1 .. 14 (RND m is 10 + m). They are the sub-tabs of two function keys (LFO: LFO 1 .. 8; `LFO 9`: LFO 9, LFO 10, RND 1 .. 4). Each tab holds the four screen instances in order, the markers, the instance picture, the caption `Native Caption <view> title` (22 91 400 44) and the sentinel. **Pictures:** `../../screens/lfo.jpg`, `rnd.jpg`.

## What it shows

- `lfoScreen`: the modulation editor and targets title and keyline (shared).
- `lfoScreenTop`: `lfoNativeFullEditor` (0 0 1280 628): the field groups of the LFO (rate, timing HZ / BPM / KEY, phase, delay, mode, motion, key follow, shape; the random LFO fields), the shape editor controls (BACK, the seven tools, SHAPES, MORE, UNDO, REDO, DELETE, MOD, SNAP, LOOP, SMOOTH, CYCLE LINK, LOOP LINK, POINT, X, Y, CURVE, SCALE, GRID X, GRID Y, PHASE, BEND ALL, MODE) and the detail rows of a point modulation.
- `lfoScreenTop2`: the text lines of the editor (selected, hint, grid, trigger, info, status, marker hint, point modulation summary).
- `lfoScreenTop3`: `lfoNativePicks` (ADD SOURCE, ADD TARGET group and the target lists) and the native keyboard (`native keyboard overlay`) for the shape name.

The LFO shape and the live cursor are drawn by the plugin; there is no selector cell on the page.

## LFO and RND

One component. The bound LFO (1 .. 10) or random LFO (11 .. 14) decides which fields apply: the random LFO shows the style grid, RATE, HZ / BPM / KEY, STEREO and the trigger; the shape editor applies to the LFOs. Availability of the optional editor fields is a published handle set (`lfoeditor::kAvailability`).

## Data

Blocks: `kLfoBase` (per-LFO values), `kLfoEditor` (`lfoeditor::kView`, `kValues`, `kAvailability`, `kScale`; `shell-mpc/native_lfo_editor.h`), `kLfoDetail` (`native_lfo_detail.h`), `kLfoPick` (`native_lfo_pick.h`), `kLfoTargetsAction`, `kModTargets`, the text lines `kNativeText` / `kDrawText`, the keyboard handles `kTextEdit` (`native_text_entry.h`), the field cells `native_lfo_controls.h`. The screen's handles are the same for all 14 tabs.

## Q groups

LFO: RATE, TIMING, MOTION, KEY FOLLOW (key follow sync only), SHAPE (SMOOTH, HOST). RND: RANDOM (style grid, RATE, HZ / BPM / KEY, STEREO) and TRIGGER (MODE), KEY FOLLOW. Shape editor: EDIT, GRID, TRIGGER (MODE, PHASE, BEND ALL). The targets and modulations pages: one group per row.

## C++ owners

`shell-mpc/overlay/lfo_ui.h`, `lfo_layout.h`, `lfo_shape_files.h`, `native_marker_covers.h`; `shell-mpc/lfo_live.h`, `native_lfo_controls.h`, `native_lfo_editor.h`, `native_lfo_detail.h`, `native_lfo_pick.h`.
