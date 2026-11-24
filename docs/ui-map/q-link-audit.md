# Q-Link coverage audit (2026-11-21, read-only; XL candidate 13492646 snapshot)

Sources: `.hw-local/owner-cand/q-checklist-xl.md` and `q-status-xl/status-*.txt` (a device snapshot on Sbtest: partly stale, e.g. FX is from before the FX pilot, Env/LFO/Filter merges are not merged yet), `docs/MPC.md` 'Q groups' (Key 61: bank n = Q 4n-3..4n of the same 16-slot list, the banks equal the groups a page publishes, at most 4; XL: no active group, 4 groups fit, more groups are paged by touch). Anything marked UNVERIFIED is inferred from docs, not from code or a device. Stepped targets are driven relatively (one step per ~6 clicks), so list-type controls are usable on a knob; virtual Q targets exist (`kVirtualRow` in `mod_ui.h`, the Voice selectors), so a selector can be a Q target.

| Page | XL used / free | Key 61 banks | Uncovered on-screen controls | Proposal |
|---|---|---|---|---|
| Play | 12 / 4 (slots 13-16) | 3 | Osc power `oscN.on`, route `.destination`; compact LFO sync `lfoN.sync`, mono `lfoN.mono`, TRIP/DOT; the three carousels | 4th group on Q13-16 / bank 4: [LFO SYNC, OSC POWER, LFO MONO, selector] (selector-as-Q UNVERIFIED) |
| Macros | 8 / 8 | 2 | none | nothing |
| Osc A/B/C (wavetable) | 14 / 2 | 4 | Warp 1 and 2 (`oscN.warp.type/.amount/.phase`, `.warp2.*`, 6 controls in groups 5-6 of 6), power, route | replace the weak [-, Phase, Rand, -] group with [WARP1 TYPE, WARP1 AMT, WARP2 TYPE, WARP2 AMT]; Phase/Rand to MORE or a G4 |
| Sub | 13 / 3 | 4 | power only | nothing |
| Noise | 10 / 6 | 3 | sample mode START `noise.phase` (Playback group Q4 empty) | Playback group = [START, RANDOM, LOOP, BOUNCE] |
| Env (after the merge) | 11 / 5 | 3 | all knobs covered; only the ENV 1..6 selector is touch-only | selector as a virtual Q (UNVERIFIED) |
| LFO (after the merge) | 14 / 2 | 4 | key follow `lfoN.keytrack_transpose/_tune` (KEY sync only), TRIP/DOT, selector | KEY has TRANSPOSE and TUNE. SMOOTH has its own switch target. HOST is touch-only in TIMING. On XL, Q4/Q5 control Smooth time and enable, followed in KEY mode by Q6/Q7 for transpose and tune. |
| FX | 4-8 / 8-12 | 2 | compressor BAND selector, power, MORE rows off-screen | publish the MORE rows as groups 3 and 4 on the XL (feasibility UNVERIFIED) |
| Mixer | 16 / 0 | 4 | 10 strips x Level/Pan/Bus 1/Bus 2; only 4 strips on the knobs at once | option: a Levels-only layout [Sub, A, B, C] and [Noise, F1, F2, Main] |
| Filter (after the merge) | 12 / 4 | 3 (4 with Formant) | MODEL `filterN.model`, STYLE `.style`, CLEAN `.clean_mode`, POWER, FORMANT RESONANCE (Formant only) | OUT = [BLEND, ROUTING, MODEL, STYLE]; formant resonance into the Formant group |
| Mod | 4 / 12 | 1 | `modNN.bipolar`, `modNN.bypass`, aux source / scale | G2 = [BIPOLAR, BYPASS, AUX SRC, AUX SCALE]; G3-G4 = the next visible rows (UNVERIFIED) |
| Master | 14 / 2 | 4 | GLIDE group TIME, SLOPE, ALWAYS, SCALED; LIMIT, OVERSAMPLE, NO SMOOTHING, PAD PRESS/% | 4 groups fit exactly: drop VOICE OPTS (3) or merge with PITCH to expose GLIDE: owner decision |
| Voice | 12 / 4 | 4 | TARGETS `voicectl.osc.a/b/c/sub`, NOISE (groups 5-6 of 6); RAND 10x group has 1 member | pack to 4 groups: [STEP, ROW, LENGTH, VALUE], [RANDOM x4], [10x, ENV TIME, LFO TIME, LFO MODE], [OSC A, B, C, SUB] |

## Top 10 by gain
1. Osc A/B/C: Warp 1 and 2 on the knobs (swap out the weak group).
2. Filter: MODEL and STYLE on OUT; formant resonance.
3. FX: expose the MORE rows as groups 3-4 on the XL.
4. Voice: repack so the TARGETS are reachable at once.
5. Mod: polarity and bypass + the next rows as groups.
6. Play: a 4th group (LFO sync, osc power, mono, selector).
7. Master: the GLIDE group (needs the drop decision).
8. LFO: key follow in the free slots of group 4.
9. Env / LFO selectors as virtual Q after the merge (UNVERIFIED).
10. Noise sample mode: START in the Playback group.
Nothing to add: Macros, Sub, Env knobs; the Mixer fits only 4 strips at once under the router rule.

Doc inconsistencies found: `osc.md` Noise Playback lists START at Q2 in the controls table but not in the group list; Master LINK off lets BEND DOWN reappear and shifts the free slots.

## Status of the proposals (2026-11-21, branch `work/qlink-map`)

The table above is the state BEFORE the mapping extension; this is what was done. Every changed page has host tests for BOTH devices (`shell-mpc/overlay/tests/qslots_host.cpp` `expectGroups`: the XL with 16 knobs and no active group, the Key 61 with four knobs, the active group first and the banks 1 .. 4 following), the chips follow the Q slot numbers, and `qlinkBoundsData` stays `[]` (no skin file was touched).

| # | Item | Status | Where / test |
|---|---|---|---|
| 1 | Osc A/B/C wavetable: Warp 1 + 2 | DONE, kept in the bold cut step 7 (the WARP group is [WARP 1 TYPE, WARP 1 AMT, WARP 2 TYPE, WARP 2 AMT] in the default view; a warp that is Off has no amount and the group closes up; the PHASE group below is gone: PHASE, RAND and the warp phases are on MORE) | the weak type-bar group is the WARP group [WARP 1 TYPE, WARP 1 AMT, WARP 2 TYPE, WARP 2 AMT]; PHASE, RAND and the two warp phases are a last group PHASE (five groups; the XL pages the fifth by touch); the other oscillator types keep their groups; no layout change (`ui/screen.h` `buildGroups`; `qslots_host` `testOscillator`, `ui_screen_host`, `osc_ui_host`) |
| 2 | Filter OUT = [BLEND, ROUTING, MODEL, STYLE] | DONE (step 4, branch `work/step4-filter`, 2026-11-21): OUT is frameless [BLEND, ROUTING, MODEL, STYLE], the formant RESONANCE is a Q member of TRACK in the place of VAR; `qslots_host` proves both devices and every state, the `coverage_host` Filter notQ list is `on`, `clean_mode` | `filter_ui.h` and the filter screens are not this worker's files |
| 3 | FX MORE rows as groups 3 and 4 (XL) | DONE (small change, see Feasibility) | `fx_ui.h` `publishQGroup`: ALL rows of the effect are groups (`qslots_host` `testFx`, both devices) |
| 4 | Voice repack to four groups | DONE | STEP, RANDOM, [10x, ENV TIME, LFO TIME, LFO MODE], [OSC A, B, C, SUB]; NOISE touch only (`qslots_host` `testVoice`, `mastervoice_ui_host`) |
| 5 | Mod matrix: BIPOLAR / BYPASS / AUX and the next rows | DONE | four groups, `matrixGroupTargets` in `q_publish_mod.h` (`qslots_host` `testModMatrix`, `coverage_host`: the two gaps are closed) |
| 6 | Play: a fourth group | DONE for [LFO SYNC, OSC POWER, LFO MONO, -]; the selector is NOT published | the mechanism supports a selector (see Feasibility) but which of the three selectors it is, is an owner decision; the three members are STEPPED targets, one click never switches them (`q_step_rule_host` `testNudge`, `env_ui_host` `testPlayPageQSlots`) |
| 7 | Master GLIDE | DONE (owner may reverse) | the GLIDE group replaces VOICE OPTS on the Q-Links (VEL TRACK, SAME NOTE, STEAL stay visible and touch-editable); MAIN = OUTPUT, PITCH, VOICES, GLIDE (`qslots_host` `testMaster`, `mastervoice_ui_host`) |
| 8 | LFO key follow group | HANDED TO step 3 (LFO owner) | `lfo_ui.h` is not this worker's file |
| 9 | Env / LFO selectors as virtual Q | FEASIBILITY ONLY (yes), not implemented | see Feasibility; `env_ui.h` / `lfo_ui.h` belong to step 2-3 |
| 10 | Noise sample START in Playback | DONE | PLAYBACK = [START, RANDOM, LOOP, BOUNCE] and the block of the screen follows (START moved from SOURCE to PLAYBACK so the group is adjacent); SOURCE = [KIND, STEREO, ENV 1]; the `osc.md` inconsistency is fixed (`qslots_host` `testNoiseSample`) |
| - | Mixer levels-only layout | DONE | XL Q1–Q10 follow strip order: SUB, A, B, C, NOISE, F1, F2, BUS 1, BUS 2, MAIN. Four-knob banks group MAIN with NOISE/F1/F2 and keep BUS 1/2 together. PAN, sends and destinations use touch. See `qslots_host` and `mixfx_ui_host`. |

## Stepped and toggle targets on a Q-Link: one click never flips them (verified)

`QRouter::hostWrite (..., stepsOf)` (`shell-mpc/q_router.h:202`) drives every target with `stepsOf >= 2` RELATIVELY: the host's travel is accumulated (`acc_`) and one step of the target happens per `kStepTravelDefault` = 6 / 128 of travel, in the direction of the turn; a write that moves the host value by less than that changes nothing, a change of direction starts the travel again, at the ends nothing happens. This holds for toggles too: a toggle is `stepsOf == 2` (`q_step_rule.h` `stepCountOf`: every `stepped` parameter), there is no special case that flips on the first click. The plugin passes the rule in (`kit/synthyboy_params.h:54`, `kit/q_targets.h:123`). A write of up to `kMaxStepsPerWrite` (4) steps worth of travel counts as a turn, a bigger jump is no turn at all. Proven for the real parameters `oscN.on`, `sub.on`, `noise.on`, `lfoN.sync`, `lfoN.mono`, `voicectl.osc.*`, `voicectl.rand.detune10x`, `modNN.bipolar`, `modNN.bypass` from both toggle states by `shell-mpc/tests/q_step_rule_host.cpp` `testNudge`: 1 click changes nothing, a jiggle (+1 -1 +1 -1 +1) changes nothing, 5 clicks change nothing (one by one or in one coalesced write), the 6th click makes exactly one step. No extra threshold was added: the 6-click rule already applies; a stronger one for OSC POWER alone would be a per-parameter table in `q_step_rule.h` (owner decision: after 6 clicks the oscillator does switch off).

## Feasibility (read-only probes, 2026-11-21)

**(3) FX MORE rows as groups 3 and 4 on the XL: YES, implemented (small).** Before, `FxUi::publishQGroup` (`shell-mpc/overlay/fx_ui.h:814`) published only the rows of the view on screen (`firstRowOfPage` / `rowsOnPage`). Now it publishes all `numRows()` rows (default view first, MORE rows after): ~10 lines, no change to `fx_layout.h`, `fx_slot_editor.h` or the router. The XL keeps up to four rows where they are (Q 1 .. 16), a bigger table is paged like on the Key 61 (banks 3 and 4 = MORE rows). Risks: (a) the rows of the other view have no control on the screen: no chip, the knob works and the XL's knob display shows the parameter name (the same compromise as the aux pair of the Mod matrix); (b) the compressor's BAND selector stays view-local (no Q slot, unchanged); (c) `fx_slot_editor.h` `groups[]` of the MORE rows exist while the default view shows (`updateGroups`), so the blocks of the idle chips are right when the page switches; (d) the FX pilot owner may prefer the old behaviour: it is one hunk to revert.

**(9) Env / LFO selector as a virtual Q target: YES, mechanism supports it; NOT implemented (step 2-3 owner files).** Mechanism: `QRouter::kVirtual` (`shell-mpc/q_router.h:41`: ids 0 .. 9, the page tells the value with `setQVirtual`, reads the host's write with `takeQVirtual`; used by the Voice STEP / ROW (`voice_ui.h:92-121`) and the Mod row (`mod_ui.h:136-138`)). The LFO editor takes ids 0 .. 9 but only while its editor pages are open (`lfo_ui.h:63`, `1288-1300`, `1996-2005`); the Env page and the LFO main page may use id 0 (the router has one value per id, pages publish one at a time; the Play main state publishes only the compact groups). What would change: the page publishes `encode (0, kVirtual)` in a free slot (Env 11/5 free, LFO 14/2 free before item 8) and maps the value to the selection (`env_ui.h:97`, `lfo_ui.h:127`; after the merge the `ui::UsedSelector` list: the value is `index / (count - 1)` of the SHOWN entries, count = used + the next empty one). Risks: (a) a virtual target is ABSOLUTE (`q_router.h:208` gives it no step count: the comment at line 200 mentions a `setVirtualSteps` that does not exist), so one entry is 128 / (n - 1) clicks of the knob (6 envelopes: 25 clicks; fine as on the Voice STEP knob) and the count changes when the used list grows; (b) every selection change re-fills the page (display recompute): scrubbing the knob selects 5 times in a turn, acceptable but to be measured on the Key 61; (c) on the Play page the selector of the third is the host's own parameter (`inst.playSelection`), so a virtual target there would have to write it, a separate decision; (d) the LFO selector after the merge also holds the random LFOs (RND 1 .. 4).

**Play fourth group, selector member: mechanism yes (same as above), not published.** Which carousel (oscillator, envelope or LFO) is the owner's pick; the group stays [LFO SYNC, OSC POWER, LFO MONO, -] until then.

## Open for the owner

- Item 7 is applied as the coordinator recommended (GLIDE replaces VOICE OPTS on the Q-Links); owner may reverse (`voice_master.h` `buildGroups`: one line).
- Play selector: which of the three carousels (or a fourth member at all).
- OSC POWER: after 6 clicks it switches off; a stronger threshold for it alone is possible (per-parameter table), the default is one rule for every stepped target.
- The Mixer got a third group [BUS 1, BUS 2] (the brief had two groups of four; without it the bus levels would have lost their knobs).
