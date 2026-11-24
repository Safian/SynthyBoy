# Osc pages and Play page of the bold cut: before / after

Owner decision 2026-11-21 ("Bold cut"), step 7: the Osc pages (Osc A, B, C, Sub, Noise) and the Play page; spec `docs/ui-map/SIMPLIFY.md` (B, B2, B3, Play row), selector rules `docs/ui-map/env-filter-lfo-plan.md`. Pictures: `ui_gallery_host` state Init, 1280 x 800, 16 Q-Link knobs (XL chips), quantised to 128 colours. `before/` = develop 6e89be80, `after/` = this branch (`work/step7-osc-play`). Nothing is removed from the engine or the parameter table: every hidden control is on MORE (or its detail pages) or on its home page, and `coverage_host` checks that every parameter still has a control and a Q member somewhere.

## The default view per type (the counts)

Counted as the user sees controls: POWER included, MORE and the read-only indicator not, the unison box is one control (voices; its DETAIL gear opens the unison page), a warp slot is its type and its amount. "Before" is a recount of the old layout from the code (top band with PAN, LEVEL and ROUTE, the type bar, the tune column with MORE, the knob panel); the "about 74" of `SIMPLIFY.md` counted cells differently.

| type | before | after | target (`SIMPLIFY.md`) | default controls after |
|---|---|---|---|---|
| Wavetable | 27 | 12 | 12 | POWER, TYPE, TABLE, WT POS, OCT, UNISON, DETUNE, FIN, WARP 1 TYPE + AMOUNT, WARP 2 TYPE + AMOUNT |
| Granular | 26 | 12 | 12 | POWER, TYPE, SOURCE, SCAN, OCT, DENS, LENGTH, WARP, OFFSET, DIR, PITCH, FIN |
| Multisample | 27 | 10 | 10 | POWER, TYPE, SOURCE, TIMBRE, OCT, UNISON, DETUNE, FIN, WARP 1 TYPE + AMOUNT |
| Sample | 27 | 10 | 10 | POWER, TYPE, SOURCE, SCAN, OCT, UNISON, DETUNE, FIN, WARP 1 TYPE + AMOUNT |
| Spectral | 27 | 9 | 9 | POWER, TYPE, SOURCE, POSITION, OCT, UNISON, DETUNE, WARP 1 TYPE + AMOUNT |
| Sub | 14 | 8 | 8 | POWER, WAVEFORM, OCT, CRS, PHASE, ENV 1, MONO, FREE RUN |
| Noise, generated | 12 | 9 | 8 to 11 | POWER, KIND, SOUND, FILTER, STEREO, PITCH, FINE, KEY TRACK, ENV 1 |
| Noise, sample | 16 | 11 (12 with LOOP on) | 8 to 11 | POWER, KIND, FILTER, STEREO, PITCH, FINE, KEY TRACK, ENV 1, START, RANDOM, LOOP (+ BOUNCE) |

A warp whose type is Off hides its amount (so the wavetable shows 10 to 12 controls, the others 8 to 10); the numbers above count a warp that is on. Noise: `SIMPLIFY.md` counted "SOUND/KIND" as one; they are two parameters (`noise.type`: noise or sample, `noise.sample`: which noise), so the generated noise has 9; SOUND does not apply to a sample and is hidden there.

## What every default view has (all types)

| control | before | after | where |
|---|---|---|---|
| first Q group (Basic) | [TYPE, SOURCE, PAN, LEVEL] in a header row | [TYPE, SOURCE, the position knob, OCT] in the same band (Sub [WAVEFORM, OCT, CRS, PHASE], Noise [KIND, SOUND, FILTER, STEREO]) | `shell-mpc/ui/screen.h` `OscTopRow` |
| LEVEL, PAN | knobs in the header, Q members | read-only indicator (level in dB, pan), no touch, no Q slot; their home is the Mixer strip | `OscMixIndicator`; Play page's compact view keeps them (intentional shortcut) |
| ROUTE (destination) | button, opened the route popup (target, F1 F2 balance, BUS 1, BUS 2) | the destination in the indicator, read-only; the F1 / F2 balance is on MORE (`osc1.filter_blend`, Sub and Noise too); the sends are the Mixer's; the popup stays the Play compact view's route shortcut | `osc_spec_page.h` `kMoreCtls`, `kOutputCtls` |
| POWER | top band toggle | unchanged (touch only; `osc1.on` is also on OPTIONS) | `OscillatorFullScreen::power` |
| type bar (FRAME, PHASE, RAND, MAP, ...) | a strip with 3 to 4 controls under the header | gone: PHASE, RAND, the page buttons (MAP, ENV, REGION, SLICE, FILTER, SCAN, GRAIN, WINDOW) are on MORE, the FRAME read-out is dropped (the display shows the frame) | `oscspec::kMore` |
| tune column (OCT, SEM, FIN, CRS, TRACK, MORE) | right column, 6 controls | OCT in the first group, FIN in the bottom band; SEM, CRS, TRACK on MORE | `osc_ui.h` `bindFull` |
| MORE | button in the tune column, opened the options page (seven blocks, 21 controls) | ONE MORE button right of the bottom band, opens the MORE page (the five types in one page, hidden controls per type); the former options page is OPTIONS behind it | `oscspec::kMore`, `oscspec::kOptions` |
| BLEND, warp PHASE knobs, warp 2 (not wavetable) | on the panel | MORE | `kMoreCtls` |
| warp that is Off | amount and PHASE dimmed | amount hidden, not a Q member (the group closes up) | `OscillatorFullScreen::setWarpApplies`, `WarpSlot::setApplies` |
| display | 78 percent of the width (the type bar and the tune column beside it) | the whole width, 296 px tall on a tab | `OscillatorFullScreen::layout` |
| unison box gear label | MORE | DETAIL (the screen has one MORE) | `screen.h` |
| Q groups | 5 (wavetable) to 6 | 2 to 3 per type | see `docs/ui-map/osc.md` "Q-Link groups" |

## Per type: what moved to MORE

| type | MORE (the controls that left the default view) |
|---|---|
| Wavetable | SEM, CRS, TRACK, BLEND, warp PHASEs, PHASE, RAND, MAP |
| Multisample | SEM, CRS, TRACK, BLEND, WARP 2 (type, amount, phase), warp 1 PHASE, RAND, ENV, VEL TRACK (OVERRIDE, amount) |
| Sample | SEM, CRS, TRACK, BLEND, WARP 2, warp phases, PHASE, REGION, SLICE |
| Spectral | SEM, CRS, TRACK, FIN, BLEND, WARP 2, warp phases, PHASE, FILTER, SCAN |
| Granular | SEM, CRS, TRACK, VOICES, UNISON page, LEN RAND, PAN RAND, LVL RAND, LOOP, GRAIN, WINDOW (no BLEND: a granular type has no unison blend control, as before) |
| Sub | BLEND (`sub.filter_blend`); the destination and the sends are the Mixer's |
| Noise | BLEND (`noise.filter_blend`); the destination and the sends are the Mixer's |

## Hidden, not dimmed

| control | hidden unless |
|---|---|
| warp 1 / warp 2 amount | its type is not Off |
| SOUND (Noise) | the noise is generated (a sample has no sound choice) |
| START, RANDOM, LOOP, BOUNCE (Noise) | the noise plays a sample |
| BOUNCE | Noise sample mode (ping-pong takes precedence over LOOP) |
| W1 PHASE, W2 PHASE, W2 AMOUNT (MORE) | the warp's type is not Off |
| PHASE, RAND, VEL TRACK, LOOP, ... (MORE) | the type has the control (`oscspec::Cond`) |

## The Play page

| control | before | after | where |
|---|---|---|---|
| Env carousel and its list | all six entries | the used envelopes, the selected one and ONE `+` (empty preset: ENV 1, `+`); the cells are made from the same filled `UsedSelector` the Env tab shows (`uibind::cellsFromSelector`), not a copy | `env_ui.h` `pullSelector`, `ui_bind.h` |
| LFO carousel and its list | all fourteen (LFO 1 .. 10, RND 1 .. 4) | the used LFOs and random LFOs, the selected one and ONE `+`; `+` with two free kinds opens the two-row list over the third (`LFO n` / `RND m`) | `lfo_ui.h` `selectSlot`, `openInstanceList` |
| a `+` pick that is not edited | stayed selected | falls back to the last used item (in number order; else the first used after it, else ENV 1 / LFO 1) when the TAB is left; view state only, no parameter is written. Decision 2026-11-21, tested | `ui_bind.h` `fallbackSelection`, `page_view_base.h` `leave` hook, `leftTab` of `EnvUi` / `LfoUi` |
| 4th Q group, LFO MONO | built for a random LFO too (`rlfoN.mono` does not exist: the slot was empty by accident) | exists only for a standard LFO; for a random LFO the group is [LFO SYNC, OSC POWER, -, -] by design | `play_view.h` `extraTargets` |
| compact oscillator (level, pan, on, destination, type, source), compact Env / LFO knobs | - | unchanged (intentional shortcuts, owner (e)) | - |

## Pictures

The main states, before / after. Wavetable [before](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/before/Init_osca_type_wave.png) / [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/after/Init_osca_type_wave.png); both warps on before (reference retired) / [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/after/Init_osca_warp_on.png), spectral with warp 1 on [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/after/Init_osca_spec_warp_on.png); multisample [before](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/before/Init_osca_type_multi.png) / [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/after/Init_osca_type_multi.png); sample [before](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/before/Init_osca_type_sample.png) / [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/after/Init_osca_type_sample.png), loaded [before](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/before/Init_osca_type_sample_loaded.png) / [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/after/Init_osca_type_sample_loaded.png); spectral [before](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/before/Init_osca_type_spec.png) / [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/after/Init_osca_type_spec.png); granular [before](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/before/Init_osca_type_grain.png) / [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/after/Init_osca_type_grain.png), loaded [before](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/before/Init_osca_type_grain_loaded.png) / [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/after/Init_osca_type_grain_loaded.png); Sub before (reference retired) / [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/after/Init_oscsub.png), all switches on [before](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/before/Init_oscsub_all_on.png) / [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/after/Init_oscsub_all_on.png); Noise generated before (reference retired) / [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/after/Init_oscnoise.png); Noise sample, loop on [before](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/before/Init_oscnoise_sample_loop_on.png) / [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/after/Init_oscnoise_sample_loop_on.png), loop off (BOUNCE hidden) [before](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/before/Init_oscnoise_sample_loop_off.png) / [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/after/Init_oscnoise_sample_loop_off.png); Osc B before (reference retired) / [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/after/Init_oscb.png), Osc C before (reference retired) / [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/after/Init_oscc.png).

MORE per type: wavetable [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/after/Init_osca_wave_more.png), multisample [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/after/Init_osca_multi_more.png), sample [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/after/Init_osca_sample_more.png), spectral [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/after/Init_osca_spec_more.png), granular [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/after/Init_osca_grain_more.png); OPTIONS [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/after/Init_osca_wave_options.png); the detail pages the MORE buttons open: wavetable MAP [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/after/Init_osca_wave_map.png), sample REGION / SLICE [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/after/Init_osca_sample_region.png) / [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/after/Init_osca_sample_slice.png), granular GRAIN [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/after/Init_osca_grain_grain.png), spectral FILTER / SCAN [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/after/Init_osca_spec_filter.png) / [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/after/Init_osca_spec_scan.png).

Play page: Init before (reference retired) / [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/after/Init_play1.png) (empty preset: the Env carousel has ENV 1 and `+`, the LFO carousel LFO 1 and `+`); partly used (ENV 2 routed, ENV 5 edited, LFO 3 routed, RND 2 edited) [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/after/Init_play1_selector_partly.png); a random LFO selected [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/after/Init_play1_selector_random.png); all used [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/after/Init_play1_selector_all.png); the lists opened by a tap on the name: Env [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/after/Init_play1_selector_list_env.png), LFO [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/osc-play-bold-cut/after/Init_play1_selector_list_lfo.png). The other Play pictures (sources, Env, LFO, carousel positions, off states, type lists) are in `before/` and `after/`.

## Tests

`ui_screen_host` (`OscillatorFullScreen` for eight variants: leaves, tap reach, counts 12 / 12 / 10 / 10 / 9 / 8 / 9 / 12, groups, hidden controls are no leaves and no members, no PAN / LEVEL knob, the indicator has no touch), `osc_ui_host` (the default view's bindings, MORE, OPTIONS and the page buttons for every type, the Q groups, the read-only indicator), `osc_page_host`, `qslots_host` (every default view on both devices, the closed-up groups, MORE for all five types), `coverage_host` (every parameter still reachable: `oscN.level` and `oscN.pan` (and `sub.`, `noise.`) are "not Q" on the Osc pages by design, listed in the known gaps), `layout_lint_host` (0 findings: the MORE page and the warp block were fixed for rule g), `page_view_host`, `mouse_host`, `controller_host`, `views_host`, `env_ui_host` and `lfo_ui_host` (`testPlayCarouselIsTheTabSelector`: Play and the tab show the same list for an empty, a partly used and an all used preset; the fallback; `testPlayPageSelectorsAndExtraGroup`: the carousels of a `PlayView`, the extra Q group without a mono member for every random LFO).
