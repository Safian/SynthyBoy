# Button mutation audit (worktree branch button-mutation-audit from origin/develop; nothing committed)

Mutation (FACT): components/colors.json `button.surface` #151a2d -> #ff00ff, `button.border` #26304b -> #ffff00. These are the only tokens the Button recipe
(components/native/button/button.py:56 `action_art`, :242 `write_switch_asset`) reads. Baseline skin: /private/tmp/claude-501/btn-mutation/skin-base,
mutated skin: .../skin-mut (both `build-mpc.sh --skin-only`, ~12 s). 648 PNGs in each. Tools: diff.py, res.json in the same folder.

Note (FACT): the Button recipe has NO top keyline token; the "top keyline" exists only in the C++ overlay (theme `keyline`/`panel0`, see section C++).

## A. Skin results (FACT, pixel evidence)
Skin PNGs that CHANGED to magenta/yellow (= use button.surface/border), 25 fixed-name + 55 content-hashed `native_action_*.png` (45 show magenta, 10 are `_disabled`
copies = greyscale of the component face, grey 79,79,79: derived from the component, so they follow it):

| Screen/page | Control | Asset / code | Component? | Evidence |
|---|---|---|---|---|
| Filter 1/2 | Power, expand, TARGETS, INIT, UNDO, DELETE POINT | filter_native_power_on/off, _expand_56x56, filter_native__targets_384x56, _init_130x56, _undo_130x56, _delete_point_132x56 (98_filter_native.py:123/132 -> button.action_art) | yes | magenta 1.2k-14.9k px, yellow 52 px |
| Osc A/B/C | Power | osc_native_power_on/off (56_osc_power.py) | yes | magenta |
| FX | Chain slot power, header power | fx_chain_on/off, fx_header_power_on/off (20_fx.py:432 art() glyph=="power" -> action_art) | yes | magenta |
| LFO | Expand | lfo_native_expand | yes | magenta |
| Mod | EDIT, CONFIRM, DELETE, BACK (Mod matrix) | mod_action_edit, mod_detail_confirm, mod_detail_delete, native_action_* (99_ui_screen_mod.py:40 action_art) | yes | preview-Mod.png: BACK and DELETE magenta |
| Mixer | S / M / FX strip buttons | solo_mute_s/m/fx_states (solo_mute.py:106-135 reads button.surface/border directly, not via action_art) | yes (same tokens, own drawing code) | magenta 200 px/frame |
| Mixer | strip background bar | mixer_strip_*_bg (mixer_strip.py:476-510 button.surface/border) | token reuse, not a button | magenta 211-529 px (HYPOTHESIS: strip top bar + field wells) |
| Mixer | meter ticks | master_peak_scale (meter_block.py:167, fader.py:264 use button.border as a tick colour) | token reuse, not a button | yellow ticks; coincidence of token name |
| Play | power, full screen (expand), TARGETS, More etc. on Play/Master/LFO/Osc previews | native_action_* | yes | sheet_mut.png |
| Catalogue (components/native/*/img) | header power/expand/MORE/TARGETS/CLOSE/BROWSE, macro card F1 CUTOFF, catalog-button-48, filter-catalog-active | render_native_catalog_cards.py (ran: 10 PNGs changed, in previews/catalogue/) | yes | all header buttons magenta; BROWSE disabled grey |

## B. Controls that did NOT change = NOT using the Button component (FACT unless marked)
TUI.json colours: only `native_action_*` image names changed; no colour literal changed. `mod_row.button_surface` #151a2d remains in TUI.json (4x modNative_toggle_* background).

Group 1 - segmented "radio" faces, tokens radio.surface.top/bottom, control.border, radio.active.* (components/native/radio_button/radio_button.py:32-60), many in skin:
- Mod: ALL / BY SOURCE / BY DEST (mod_mode_mode{1,2,3}_on/off, 99_ui_screen_mod.py:34), Master HZ/BPM/KEY, Filter F1+F2 (`filter_native_dual`, 98_filter_native.py:618),
  Filter toggles CLEAN/ENV 1/LEGATO/SNAP are text-only ActiveText-like, LFO edit tools POINT/CURVE/SCALE (lfo_edit_tool_*, 53_lfo_smooth.py:350), FX chain BACK/MOVE/SOFT (fx_chain_back/move, 20_fx.py:432 glyph path),
  choice_segmented_* (84 files: Osc/Filter/Output choice rows), Mod BACK in targets (pz_1..3 BACK etc.).
  Move to component: give render_segmented_button_art a `style="button"` that reads button.surface/border, or make Button accept an active/inactive pair (cyan gradient is the "on" state, which Button has not today).

Group 2 - Button module helper `text_face()` (button.py:249) but with other tokens: mod_targets_back/active/polarity/remove (mod_row.py:539 `write_route_button_art`, surface filter.pz.active.surface #161d31, border filter.pz.active.border #525b73 / mod_targets.border.active), mod_targets_pages (mod_row.py:558), pz_* toolbar (98_filter_native.py:249).
  Looks like a button, goes through button.py, but a second token set. Fix: take surface/border from button.surface/button.border (keep only the active-border override).

Group 3 - tune.py own drawing: tune_more_up/down (tune.py:259 render_tune_more_button, field.surface/field.focus/field.border), tune_track_on/off (tune.py:224, field.surface + dot).
  Fix: draw through action_art(text="MORE") / a new `action_art(dot=...)` variant.

Group 4 - selector family (selector.py): selector_dropdown_*/selector_face_* (dropdown boxes; fields rather than buttons), filter_chain_* "CHAIN" chip (selector.py:763 render_dropdown_face), compact route button (selector.py:1105 uses button.surface/border but its only caller is a test -> dead in skin, FACT: no PNG changed), route.button.surface #0d1222 (selector.py:1123).
  Dropdowns are fields by design (HYPOTHESIS: keep separate); CHAIN chip and compact route button are buttons -> action_art with label.

Group 5 - flat square toggles via a separate token: modNative_toggle_72x56 etc. (mod_row.py:101 & :404 `mod_row.button_surface`/`button_border`, same hex as button.*, but a different token name -> not mutated). Fix: alias mod_row.button_* to button.* (or delete them).

Group 6 - play screen faces: play_stereo_on/off, play_sub_octave_*_on/off (the "-4" blue/grey cells), play_osc_modal_*, play_*_selector_frame, play_env_lfo_menu_*: all unchanged (native_play/*.py; grey #303237/#175a76 two-tone faces, own colours). HYPOTHESIS: segmented/field-like, not button recipe.

Group 7 - text-only toggles (no fill, nothing to mutate; they read as buttons to the user): active_text_on/off, masterNativeActiveText_*, voiceActive_*, voiceNativeTargets_*, osc_active_* (ON/BOUNCE/LOOP..), env_beats_*, lfo_cycle_link/loop/mono/snap/smooth/host (text on transparent). ActiveText = components/native/active_text.

Group 8 - cards/tiles: fx_chain_add ("+ ADD"), fx_chain_unselected/selected (teal cell), fx_chain_gallery_card, filter_chain_0/1/2 on/off (gray/blue "Parallel" strips), soundsmode.png, name_value_box_*, mod_amount_*: unchanged. "+ ADD" is a real button (20_fx.py:432 art(...) non-power path -> render_segmented_button_art).

Previews (FACT): render-mpc-skin-preview.py on both skins, 17 tabs (previews/base, previews/mut): Mod: BACK/DELETE magenta, ALL/BY SOURCE/BY DEST not; Filter 1: power magenta, F1+F2 and CHAIN not; FX: chain power magenta, MOVE and "+ ADD" not; Master: HZ/BPM/KEY not; Osc A/Play/LFO: power/expand/TARGETS magenta, dropdowns not. Sheet: previews/sheet_mut.png. Catalogue cards: previews/catalogue_sheet.png.

## C. C++ overlay (HYPOTHESIS: cannot regenerate without a build; read from code, FACT for the code paths)
There is NO ui::Button class in shell-mpc/ui. Buttons are drawn by `Pen::control()` (ui/core.h:257), tokens `theme::Look` panel0/panel1 gradient, keyline/keylineHi outline, rControl, frame, which come from colors.json `theme.<look>.panel0/keyline/...` via scripts/gen-ui-theme.py -> ui/theme_tokens.h. They are NOT connected to button.surface/button.border: a Button token change does not reach any C++ button.
Classes calling control() (ui/base.h line): Stepper 740, UpDownStepper 842, ToggleIcon 950 (power/expand icons; LedToggle, StripPower, LedSwitchProxy derive), MenuButton 1046, DetailButton 1194, RouteButton 1299/1308 (RouteDropdown in mixer_strip.h:372), PictogramSelector/TypeSelector 1485/1567, NumberField 586; also carousel.h:76, filter_display.h:428, lfo_parts.h:109, mod_board.h:125, text_entry.h:161/276, used_selector.h:123/129.
Icon colours: T.flow[0] (on), T.textFaint (off), T.negative (osc power off), T.lfo[0], T.textDim; text T.text/T.textDim. Not checked: which overlay screens draw which of these when overlay is on (needs hardware or a fixture build); also the other overlay sources (shell-mpc/overlay/*.h) that call rrect with theme tokens directly (display.h:213/549, fx_gallery.h, note_mask.h, mod_matrix.h, voice_master.h, lfo_display.h, lfo_screen.h, env_graph.h): not classified as button vs panel.
To move to one source: generate theme.<look>.panel0/panel1/keyline from button.surface/button.border for the control path (or add button.* fields to Look), and make Pen::control the only button painter; this changes all 3 looks.

## D. Python code scan (FACT)
- Old literals #151a2d/#26304b: components/colors.json (mod_row.button_*, button.switch.border), scripts/build_radio_preview_assets.py, scripts/render_mod_screen_preview.py (preview-only hard-coded).
- rounded_rectangle sites drawing button-like faces outside button.py: selector.py (16), tune.py (8), mixer_strip.py (9), name_value.py (4), radio_button.py (2), solo_mute.py (1, same tokens), mod_row.py (1), native_warp_picker.py, gen-mpc-tile.py, preview scripts (render_*_preview.py: own colours).
- switch asset button.py:242 write_switch_asset uses button.surface (inactive) but `button.switch.border` #26304b (separate token, would not follow the border mutation).

## E. Not checked
- C++ overlay drawing (section C), screen previews docs/screens/*.jpg (render_*_screen_preview.py compile c++ fixtures: skipped per brief), on-device host rendering, hit areas.
- Which exact skin tab uses each hashed native_action_* file (only name-hash diff; per-tab check via the 17 rendered tabs).
- Tabs not rendered: Env 2-6, LFO 2-10, RND, Note, Velo, Osc/Env variants (same definitions expected).
- Worktree is left with the mutation (colors.json and 10 catalogue PNGs modified), to be discarded.
