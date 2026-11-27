# RADIO FAMILY: which component when

The current [RadioButton](radio-kizaro-valaszto-preview.md) entry (segmented 88 x 56 flat cells, `radio_button.py`) is marked KIVEZETÉSRE: it takes too much room and it bypasses the Button component (docs/review/BUTTON-MUTATION-AUDIT-2026-11-27.md). The compact family below replaces it, all built from the Button / Label recipes in the old gradient look. The switch of the skin is a separate round; the catalogue pages keep the old RadioButton at the end of the list.

| Component | Use | Page |
|---|---|---|
| RadioList | many options (2 to 8 and more) one under the other, each with a LED | [radio_list.md](radio_list.md) |
| RadioGrid | 2 to 8 options in a grid (1 x N, 2 x 3, N x 1) of equal text buttons, with a group title | [radio_grid.md](radio_grid.md) |
| SlideSwitch | 2 to 4 ordered positions (L M H, Mono / Poly, LFO MAN ENV), horizontal or vertical | [slide_switch.md](slide_switch.md) |
| SteppedDial | the dial alone with 2 to 8 stops, no pictograms and no value arc | [stepped_dial.md](stepped_dial.md) |
| RotarySelector | few positions (3 to 6) that have a pictogram (waveform), on a dial | [rotary_selector.md](rotary_selector.md) |

## Footprint (px, width x height), the same task: 4 choices (HZ BPM KEY FREE)

Numbers come from the renderers (`sample_size`) and from the old RadioButton pictures (`catalog-radio-56.png` 88 x 56; the ENV group of 6 is 528 x 56). Row / button height 40 px, track 40 px, dial 48 px.

| Component, arrangement | Size | Area (px^2) | Compared with today (352 x 56 = 19 712) |
|---|---|---|---|
| today: RadioButton, 4 in a row | 352 x 56 | 19 712 | 100% |
| today: RadioButton, 4 in a column | 88 x 224 | 19 712 | 100% |
| RadioGrid 1 x 4 | 312 x 40 | 12 480 | 63% |
| RadioGrid 2 x 2 (width 184) | 184 x 88 | 16 192 | 82% |
| RadioGrid 4 x 1 (width 96) | 96 x 184 | 17 664 | 90% |
| RadioList vertical (width 120) | 120 x 172 | 20 640 | 105% (adds the LED and the selected highlight) |
| RadioList horizontal (item width 96) | 396 x 40 | 15 840 | 80% |
| SlideSwitch horizontal (200 long) | 200 x 68 | 13 600 | 69% |
| SlideSwitch vertical (160 long) | 78 x 160 | 12 480 | 63% |
| RotarySelector 4 stops, 48 px dial, title WAVE | 109 x 129 | 14 061 | 71% |

SlideSwitch and RotarySelector vary the choice by position rather than by naming every option as a button; their area includes the labels or pictograms. The vertical SlideSwitch with the PWM title (3 positions, 40 px track, 120 high) is 78 x 120 + the title row: see the catalogue table of SlideSwitch. Every example of the four new entries has its own "Helyigény" table on the catalogue page; the old RadioButton entry carries its three examples and the 4-in-a-row figure.
