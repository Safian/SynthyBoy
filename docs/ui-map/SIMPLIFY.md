# Simplification Proposal

> Scope: overlay UI only. No DSP changes. No code changes in this document.
> Owner rules applied: (1) page order Play → Macros → Osc → Env → LFO → FX; (2) a switched-off block is drawn grey and untouchable except its own switch.

---

## 1 — Duplicates to remove

Each item: what parameter is duplicated, where it lives now, which one to keep, why.

**Intentional shortcuts (Play / Macros) — keep these, say so:**
- `osc.level`, `osc.pan`, `osc.on`, `osc.destination`, `osc.type`, `osc.source`, `osc.waveform`, `osc.octave`, `noise.filter` on Play compact OSC → **keep**. Play is the daily-driver view; having the most-touched controls without opening Osc page is intentional.
- `env1.attack/decay/sustain/release` on Play compact ENV → **keep**. Same reason.
- `lfo1.rate`, `lfo1.shape`, `lfo1.fade_in`, `lfo1.smooth_time` on Play compact LFO → **keep**.
- `macro.1`–`macro.8` on Macros page → **keep** (this is their primary home).

**Duplicates to remove:**

| # | Parameter | Current locations | Keep | Remove | Why | Risk |
|---|---|---|---|---|---|---|
| D1 | `osc.level`, `osc.pan` | Osc full-screen top bar + Mixer strip | Osc top bar (primary editor) | Mixer strip copies | Mixer is the mixing console; level/pan are already in Osc. Mixer can keep them as an intentional mix shortcut — **[judgement]** owner should decide. | Low: Mixer users lose one place but nothing disappears. |
| D2 | `osc.on`, `osc.destination` | Osc top bar + Mixer strip | Osc top bar | Mixer strip copies | Same as D1. Mixer routing bar makes destination visual; strip copy adds confusion (`shell-mpc/overlay/mixer_ui.h:322-326`). | Low |
| D3 | `filter1.level`, `filter1.pan`, `filter1.destination`, `filter1.bus1_send`, `filter1.bus2_send` | Filter OUT block + Filter OUTPUT popup + Mixer strip | Filter OUT block | Filter OUTPUT popup copies of level/pan; Mixer strip copies | Level and pan are in the OUT block already on the Filter page. The OUTPUT popup (`shell-mpc/overlay/filter_ui.h:287-288`) adds nothing new (`shell-mpc/ui/filter_display.h:555`). The popup should contain only DEST + BUS 1 + BUS 2. | Low: users lose nothing functional. |
| D4 | `master.volume` | Master OUTPUT block + Mixer MASTER strip | Master page | Mixer strip fader | Volume has a full knob + peak meter on Master. Mixer's MASTER strip has a tall fader but no clip indicator. Keep both as intentional shortcut — **[judgement]**. | Low |
| D7 | LFO `stereo` appears as both a knob (`lfo1.stereo`) and a toggle (`rlfo1.stereo`) in the same visual slot | `shell-mpc/overlay/lfo_ui.h:493, 520` | The two are different params (regular vs random LFO) so both must stay, but the **label** should disambiguate | No removal, rename only: knob → "STEREO SPREAD"; toggle → "STEREO (L/R)" | Shared name in same area causes confusion (`play.md` Questionable §1). | Low |
| D8 | Env 4 tabs vs in-page EnvSelector | `overlay_layout.json:268-390` — 4 tabs for 6 envelopes | In-page EnvSelector handles all 6 | **Remove 3 redundant tabs** (keep 1 "Env" tab, remove Env 2/3/4 tabs) | The EnvSelector bar on the page already navigates all 6 envelopes (`env.md` Questionable §3). Three tabs waste navigation slots. **[judgement]** | Medium: users who open Env 2 by tapping the tab must now use the in-page selector. |
| D9 | Filter 2 host tab | `overlay_layout.json:47-119` — 2 tabs for 2 filters | In-page CURVES selector handles both | **Remove "Filter 2" tab**, keep "Filter" (or "Filter 1") | The CURVES selector (F1 / F2 / F1+F2) lets you switch on either tab (`filter.md` Questionable §3). Second tab wastes a navigation slot. **[judgement]** | Medium: same as D8. |
| D10 | MODEL stepper + MODEL list button (adjacent, same action) | Filter top bar: `shell-mpc/overlay/filter_ui.h:256-257`, `shell-mpc/ui/filter_display.h:770-771` | MODEL stepper (center tap already opens list) | **Remove dedicated `modelList` arrow button** (56 px) | Stepper center tap opens the same popup; the separate button is redundant (`filter.md` Questionable §2). | None |

---

## 2 — Useless / confusing / dead controls

Verified against cited file:line before listing.

| # | Control | Location | Verdict | Why | Risk |
|---|---|---|---|---|---|
| U1 | `monoTop` ToggleIcon (dead "MONO" in LFO full-screen) | `shell-mpc/ui/lfo_screen.h:135, 192` — `show(monoTop, false)` unconditionally | **Delete** | Hidden unconditionally since code was written; `monoQ` in MOTION block is the real control (`lfo.md` Questionable §1). **Confirmed**: `show (monoTop, false)` at line 192. | None — it is invisible. |
| U2 | `style` Stepper in LFO full-screen (dead) | `shell-mpc/ui/lfo_screen.h:142, 193` — `show (style, false)` | **Delete** | Comment in source says "the style grid IS the style control (a stepper beside it was the same control twice)" (`lfo.md` Questionable §2). **Confirmed**: line 193. | None |
| U3 | `shape` and `sync` steppers in LFO full-screen (dead) | `shell-mpc/ui/lfo_screen.h:142, 196, 358` — both unconditionally hidden | **Delete** | Picker grid handles shape; `syncSwitch.mode` handles sync (`lfo.md` Questionable §3). **Confirmed**: line 196. | None |
| U4 | COMB knob in Filter TRACK block (inert for 26/27 models) | `shell-mpc/overlay/filter_ui.h:878`, TRACK block Q4 slot | **Hide when not Comb model** (use model-conditional show, like FORMANT block already does) | Permanently dimmed Q4 for 26 models, wasting a hardware encoder slot (`filter.md` Questionable §4). | Low — no functional loss. |
| U5 | LEVEL + PAN in Filter OUTPUT popup | `shell-mpc/overlay/filter_ui.h:287-288`, `shell-mpc/ui/filter_display.h:555` | **Remove from popup** (keep in OUT block) | Popup's purpose is bus sends + destination. LEVEL and PAN are already the first two knobs in the OUT block (`filter.md` Questionable §1). | None |
| U6 | Spectrum strip on Macros page (read-only, no controls) | `shell-mpc/ui/mod_macros.h:22-76` | **[judgement] Remove or move** | Strip is purely visual, cannot be interacted with, appears to be a visual remnant from the host skin 32-meter row (`macros.md` Questionable §3). Saving the vertical space would let macro chips be larger. | Low — users lose a visual spectrum view. |
| U7 | "None" destination choice in oscillator/filter routing | `docs/params.json` — present in all destination lists; `mixer.md` Questionable §5 | **[judgement] Remove** | Audio is silently dropped. The power toggle already mutes the source. Routing line draws a dead-end stub. | Low — edge-case use in complex routes; could keep for audio safety. |
| U8 | Non-interactive routing bar chips on Mixer (FLT 1, FLT 2, MAIN FX…) | `shell-mpc/ui/mixer_screen.h:18` — no touch handler | **Make tappable or visually distinguish** | Chips look interactive but do nothing (`mixer.md` Questionable §4). | Low |
| U9 | Disabled "OUT" button on MASTER mixer strip | `shell-mpc/overlay/mixer_ui.h:156-157` | **Hide or remove** | A greyed-out button the same size as the other destination selectors, but does nothing (`mixer.md` Questionable §1). | None |
| U10 | `reverb.force_enable` in its own row block ("TIER") | `shell-mpc/ui/fx_layout.h:168` | **Move to OUTPUT or ALGORITHM row** | Single toggle consuming an entire 4-slot Q-Link bank and forcing Reverb to 2 pages (`fx.md` Questionable §2). | Low |
| U11 | Env `legato_inverted` label reads "LEGATO" (inverted param) | `shell-mpc/ui/env_screen.h:175`, `shell-mpc/overlay/env_ui.h:362` | **Rename label to "RETRIGGER"** (or invert the parameter write) | Turning "LEGATO" on actually enables retrigger-on-legato. Opposite of what label suggests (`env.md` Questionable §2). | Low — cosmetic. |
| U12 | Virtual Q1 row-stepper on Mod matrix (tapping already selects rows) | `shell-mpc/overlay/mod_ui.h:786` | **[judgement] Reassign Q1 to `mod01.bipolar`** | Touch row tap selects directly; Q1 as row-stepper wastes a slot while polarity is unreachable by hardware (`mod.md` Questionable §1). | Low |
| U13 | Dead `NumberField` / `T::Field` code in `EnvUi::pull()` | `shell-mpc/overlay/env_ui.h:423-437` | **Delete** | `bind()` is never called with `T::Field` in `configure()`. Confirmed by `env.md` Questionable §1 and comment in `env_screen.h:5-6`. | None |
| U14 | LFO HOST sync toggle placement (under shape picker, isolated from TIMING block) | `shell-mpc/ui/lfo_screen.h:372`, `shell-mpc/overlay/lfo_ui.h:505` | **Move HOST into TIMING block** | HOST sync belongs with HZ/BPM/KEY options; currently isolated below the shape grid, invisible in the TIMING Q-group (`lfo.md` Questionable §6). | Low |

---

## 3 — Controls to move one level deeper

Rarely used / expert-only parameters that belong behind a popup or detail page rather than the main view.

| # | Control | Current location | Proposed level | Why |
|---|---|---|---|---|
| M1 | Env `BEATS`, `LEGATO`, `HOLD`, `DELAY` | Env full-screen TIME block (Q-group 2) | Keep in full-screen (they are already one level up from Play compact); only BEATS should be surfaced in compact too | A BEATS toggle should exist in Play compact ENV so users can see ms vs beat mode without expanding. |
| M2 | Osc `MORE` options page (pitch mode, phase memory, key zone, velocity zone, transpose snap, unison shape) | Osc → Tune column → MORE button → `oscspec::kMore` | Already deep — keep | These 20+ parameters are already 2 levels deep; correct. |
| M3 | Filter `BLEND`, `LEVEL`, `PAN`, bus sends | Filter OUT block (always visible) | Move LEVEL + PAN to OUTPUT popup (or Mixer); keep BLEND and ROUTING in OUT block | Level and pan belong in the mix console (Mixer). The Filter page is for DSP parameters. **[judgement]** |
| M4 | Mod slot `AUXILIARY SOURCE`, `SHAPE`, `TIME` blocks | Mod → Slot Detail page (already one level deep) | Correct depth — keep | These are advanced modulation-slot parameters. |
| M5 | LFO Point Modulation (`PointModPage`) | LFO → ShapeEditor → MOD (3 levels deep) | No change possible without redesign — note in OVERVIEW | For live performance this is too deep but the shape editor itself is a power-user feature. |
| M6 | Master `TUNING & MPE` side (MTS-ESP, NOTE-ON, MPE bend, XYZ macros, Y bipolar) | Master TUNING & MPE side (1 tap from main) | Correct — already side-switched | These are setup-time parameters. |
| M7 | Osc `FREE RUN`, `ENV 1`, `MONO` toggles on Sub | Osc Sub bottom panel TUNE block | Correct depth | Rarely changed during performance; bottom panel is already secondary. |
| M8 | Voice `SCALING`, `TARGETS` blocks | Voice page bottom blocks | **[judgement] Move to Master → VOICES** | Only 11 parameters, rarely changed; Voice page could then focus entirely on the step grid which is its core UX. |

---

## 4 — Visual simplification per page

Default view means what the user sees without opening any popup or full-screen.

| Page | What can be hidden by default | Condition |
|---|---|---|
| **Play / Osc panel** | ROUTE button, osc-instance selector beyond the name | Show ROUTE only when osc is on; show instance list on tap-name only |
| **Play / Env panel** | Envelope sub-controls below graph (rate, division, sync, fade-in, smooth, stereo, mono, keytrack, shape, host) | These appear only in the Play compact controls block; most are shown only in full-screen already. Add BEATS toggle to compact. |
| **Play / LFO panel** | EDIT button (opens ShapeEditor — power user) | Hide for built-in shapes; show only when shape = DRAWN |
| **Osc page** | TypeBar FRAME field (read-only display, not an editable control) | Move to display overlay or tooltip |
| **Osc page** | VEL TRACK toggle + amount in Multisample TypeBar | Move into Multisample Envelope detail page |
| **Filter page** | COMB knob in TRACK block | Show only when model = Comb (already dim; should be hidden) |
| **Filter page** | FORMANT RESONANCE knob (not in Q group) | Add to FORMANT Q group by replacing an unused slot or shrinking the block |
| **Env page** | 4 separate ENV tabs (env1, env2, env3, env4) | Collapse to 1 tab + in-page EnvSelector |
| **LFO page** | TRIP / DOT toggle buttons below DIVISION stepper | Show only in BPM sync mode |
| **LFO page** | KEY TRANSPOSE / TUNE fields in RATE block | Show only in KEY follow sync mode (already implemented; confirm hidden by default) |
| **Mod page** | Quick-add source board | Currently hidden when any slot is active; add a toggle button to show board even with active slots |
| **Master page** | Group 2 "LINK" (single toggle `master.bend_down_link`) | Merge into PITCH group as Q4; free up a Q bank |
| **Mixer page** | Routing bar chips (FLT 1, FLT 2…) could be tappable or removed | Make interactive OR remove (they currently look interactive but are not) |

---

## 5 — Estimated effect: controls per page before → after

> "Controls" = independently addressable touch targets on the main view (not counting popup items or full-screen detail contents).
> Counts are estimates from the Controls tables; exact numbers require a code-level count.

| Page | Before (approx) | Removals | Additions | After (approx) | Note |
|---|---|---|---|---|---|
| Play | 87 | ~6 (dead LFO steppers listed in U1–U3 affect LFO full-screen, not Play compact) | +1 (BEATS toggle in compact ENV) | ~82 | LFO dead controls on full-screen page, not Play |
| Macros | 11 | 1 (spectrum strip, if removed) | 0 | ~10 | |
| Osc | ~74 (per tab) | ~4 (VEL TRACK → detail, FRAME read-only overlay, modelList button D10) | 0 | ~70 | |
| Mixer | 50 | ~2 (OUT button, routing bar chip interactivity correction) | 0 | ~48 | |
| Filter | 47 | ~6 (1 tab → D9, LEVEL+PAN from popup → U5, modelList → D10, COMB when inactive → U4) | 0 | ~39 | Counts the popup controls removed; main page loses ~2–3 |
| Env | ~19 | 0 (dead Field code is C++ not UI) | +1 (BEATS in compact) | ~20 | 3 tabs removed (structural, not control count) |
| LFO | ~40 | ~5 (3 dead steppers U1–U3; HOST moved into TIMING U14) | 0 | ~35 | |
| FX | ~200 | ~1 (reverb FORCE ON merged into OUTPUT row) | 0 | ~199 | |
| Mod | ~16 | 0 (Q1 reassignment is not removal) | 0 | ~16 | |
| Master | ~38 | ~1 (LINK merged into PITCH) | 0 | ~37 | |
| Voice | ~59 | 0 | 0 | ~59 | SCALING/TARGETS move is a judgement for V2 |

**Summary:** Removing dead/redundant code (U1–U3, U13) has zero visual impact. The highest user-visible gains come from:
1. Collapsing 4 Env tabs → 1 and 2 Filter tabs → 1 (structural navigation cleanup).
2. Removing the Filter OUTPUT popup LEVEL/PAN and the Filter modelList button.
3. Hiding the COMB knob when not in Comb mode.
4. Adding a BEATS toggle to the Play compact ENV panel (new).
5. Moving the Mixer routing bar chips from passive to tappable (UX correctness, not control reduction).

---

## 6 — Reviewer's addition (Claude Opus, 2026-11-21): the bolder cut the owner asked for

The list above removes dead code and a few duplicates but barely changes what the user looks at (FX ~200 controls, Osc ~74, Play ~82). The owner's goal is "less to look at". Proposal, judgement, for the owner to decide:

- **FX: essentials-first pages.** Each effect's default view shows at most 8 controls (on/off, mix, and the 4-6 parameters that define its character; e.g. Delay: time/sync, feedback, filter, ping-pong/style, mix); everything else behind one MORE button on that effect. Target ~200 -> ~70 visible.
- **Osc: one default view per oscillator type** (wavetable, sample, granular, spectral, multisample) with at most 8-12 controls; the rest stays in the existing MORE / detail pages. Target ~74 -> ~35 visible.
- **Tabs:** Env 1-4 -> one Env tab with the in-page selector (D8), Filter 1-2 -> one Filter tab with the curve selector (D9). With the owner's order: Play+Macros, Osc, Env, LFO, FX, Mixer, Filter, Mod, Master.
- **Hide what does not apply** (beyond the switched-off rule): model-specific knobs only for their model (COMB, FORMANT), sync-mode-specific fields only in that mode (TRIP/DOT, KEY TRANSPOSE), random-LFO-irrelevant controls hidden, not dimmed.
- **One home per parameter:** level/pan/destination live on the Mixer (the console) and on the module's own page only as a read-only indicator, except the intentional Play/Macros shortcuts.

Each item needs the gallery before/after pictures and the owner's yes before implementation.

---

## Decided (owner 2026-11-21): bold cut

Owner decision (2026-11-21, verbatim: "Bold cut") on section 6 plus the safe list of sections 1-4. Scope: overlay UI only; nothing is removed from the engine or the parameter table; every hidden control stays reachable (MORE / detail page / its home page), so every step is reversible. The disabled-block rule applies on top (a switched-off module is drawn grey and takes no touch except its own switch; `TODO.md`, "Kikapcsolt modulok a felületen"; one central "disabled" state in `shell-mpc/ui/`, each block bound to its `*.on`). Counts below come from `shell-mpc/ui/fx_layout.h` (FX) and the per-page maps in this folder; anything marked **unverified** is a guess to be checked in code before the step starts.

### Staging (owner, same day): PILOT FIRST

- **Step 1 = FX pilot.** Only the FX page is implemented first: the default views per effect, the MORE structure and the Q-Link groups of section A below, with the implementation impact in A.5. Deliverables of the pilot: the target list (A.2), the gallery before/after pictures (`screenlib.py`, every effect type), and the overlay thread cost before/after (baseline in `docs/MPC.md`: about 0.4 percent of a core at rest, 0.6 to 0.9 with a note; measure the same way on Key 61 and XL).
- **Everything else (sections B to J, the safe list in K, the tab merge in L) is PROPOSAL, pending the owner's yes after the FX pilot.** Nothing in B to L is to be started before that yes. The only exception that needs no pilot: pure dead-code deletions of the safe list that are invisible (U1-U3, U13) are listed in K and may be taken at any time with the owner's OK; they change no picture.
- Table columns on every page: **control (key/label)** | **Y** = stays on the default view | **MORE** = goes behind MORE / detail page | **hidden unless** = condition under which it is shown at all (hidden, not dimmed) | **read-only here** = home is elsewhere, shown only as an indicator | **note**. A dash means not applicable.

### Q-Link rule used for every proposed group

4 controls per group in Q-Link order (Key 61: one bank of 4 knobs, the bank button steps to the next group; XL: 16 knobs = the active group on Q 1-4 and the next three groups on Q 5-16, `docs/MPC.md` "Q groups"). A group has at most 4 members; read-only and hidden controls are never members (a group closes up from the right, a group with no live member disappears). On/off toggles stay touch controls on the slot card / power icon (no Q slot unless a page already gives them one).

---

### A. FX (STEP 1, PILOT): the default view per effect

**A.1 Mechanism (same for all 16 kinds).** `fxl::Layout` (`shell-mpc/ui/fx_layout.h:73-80`) gets an explicit split: the first rows are the DEFAULT view (at most 8 cells = at most 2 rows of 4, one Q group per row); the remaining rows are the MORE view. The editor already pages by rows (`FxSlotEditor::numPages/pageIndex`, `fx_slot_editor.h:91-94, 511-527`): the "PAGE n / m" stepper becomes one MORE button (and LESS on the second view) on that effect. The "dim in place" case of the rule in the header of `fx_layout.h` (lines 1-14, rule 2) becomes "always hide" (owner (d)): a control that is not live is never drawn, in the default view or in MORE; its row closes up from the right. Counting rule: default view = the cells of the editor (mix and level included, MORE button and the slot card's power icon not counted; with the power icon each effect shows at most 9 touch targets, at most 8 editor cells; if the owner counts on/off and MORE inside the 8, drop the cell marked `*` in each table). The display (curve/graph) of an effect is not a control and stays.

**A.2 Per-effect target list** (13 pool types + 3 splitters = 16 kinds; display height from `fx_layout.h`).

| Effect (kind) | Values today | Default cells | Display | Default view (character, one line why) |
|---|---|---|---|---|
| Chorus (1) | 12 | 6 | none | RATE/DIV, SYNC, DEPTH, FEEDBACK / VOICES, MIX: voices+depth+rate are what a chorus is |
| Compressor (2) | 24 | 8 | 220 px | MIX, ATTACK, RELEASE, BANDS / LIMIT, MID thr, MID ratio, LEVEL: speed + band count + the one band that always exists |
| Delay (3) | 14 | 6 (7 max) | 176 px | STYLE, SYNC, TIME/DIV, FEEDBACK / CUTOFF, MIX: time, feedback, tone, style |
| Distortion (4) | 9 | 5 to 7 | 160 px | TYPE, DRIVE, MIX, LEVEL / FILTER, CUTOFF, RESONANCE: type+drive define it; filter fields only when the filter is on |
| EQ (5) | 18 | 7 | 160 px | LOW/MID/HIGH CUTOFF + GAIN, LEVEL: the 3 bands as gain+frequency; modes, Q, mid 2, law in MORE |
| FX Filter (6) | 18 | 6 to 8 | 160 px | MODEL, STYLE, CUTOFF, RESONANCE / DRIVE, MIX (+ model fields): the filter's character is model+cutoff+reso |
| Flanger (7) | 9 | 6 | none | RATE/DIV, SYNC, DEPTH, CENTER / FEEDBACK, MIX: manual centre + feedback make the flange |
| Phaser (8) | 11 | 6 | none | RATE/DIV, SYNC, DEPTH, CENTER / FEEDBACK, MIX |
| Reverb (9) | 20 + 1 | 6 to 8 | none | MIX, TYPE, SIZE, DECAY / PREDELAY, WIDTH (+ ROOM, FEEDBACK when the algorithm has them): space, tail, width |
| Hyper/Dimension (10) | 8 | 6 | none | RATE, UNISON, DETUNE / DIM SIZE, DIM MIX, MIX |
| Utility (11) | 10 | 6 to 7 | none | WIDTH, PAN, LPF, HPF / MONO BASS, (MONO FREQ), MIX: the 6 jobs people use it for |
| Bode (12) | 14 | 6 | none | SHIFT, RANGE, DIR, BALANCE / FEED, MIX: frequency shift is the effect, feedback makes the spiral |
| Convolve (13) | 14 | 7 | none | IR, SIZE, TONE, PRE-DELAY / ATTACK, DECAY, MIX |
| Splitter L/H (14) | 4 | 4 | 206 px | already within the limit, no MORE: SPLIT FREQ, LOW BYPASS, HIGH BYPASS, LEVEL |
| Splitter L/M/H (15) | 6 | 6 | 240 px | no MORE: LOW SPLIT, HIGH SPLIT, LEVEL / 3 bypasses |
| Splitter M/S (16) | 3 | 3 | 206 px | no MORE: MID BYPASS, SIDE BYPASS, LEVEL |

Totals (pool effects, base case): 183 values today -> 83 default cells (about 55 percent fewer); with the splitters 196 -> 96. The reviewer's earlier target (about 70) is not reachable without cutting characterful controls; I chose 6 to 8 per effect. Max per view is 8 (limit met: no effect above 8; Compressor and a Formant-model FX Filter and Reverb type "room" reach 8 exactly).

**A.3 Tables per effect.** `key` after the prefix of the effect ("chorus." etc.). Q groups listed under each table = the default rows in order; MORE rows follow in groups of four (existing titles reused).

*Chorus* (default 6 of 12; Q: G1 [RATE/DIV, SYNC, DEPTH, FEEDBACK], G2 [VOICES, MIX])

| control | Y | MORE | hidden unless | read-only | note |
|---|---|---|---|---|---|
| rate / division | Y | - | one cell: RATE when sync=Free, DIVISION when Tempo (existing pair) | - | - |
| sync | Y | - | - | - | needed to reach DIVISION |
| depth, feedback, voices, mix | Y | - | - | - | - |
| delay1, delay2, filter_cutoff, filter_spread, level | - | MORE | delay2 only if voices > 1 (**unverified**) | - | level = output trim |

*Compressor* (8 of 24; G1 [MIX, ATTACK, RELEASE, BANDS], G2 [LIMIT, MID LOWER THRESHOLD, MID LOWER RATIO, LEVEL*])

| control | Y | MORE | hidden unless | read-only | note |
|---|---|---|---|---|---|
| mix, attack, release, bands, limit | Y | - | - | - | - |
| mid.lower_threshold, mid.lower_ratio | Y | - | - | - | the mid band exists in every band count |
| level | Y* | - | - | - | gain staging; the `*` cell to drop for a strict 8 incl. MORE |
| mid.upper_threshold, mid.upper_ratio, mid.gain | - | MORE | - | - | - |
| low.* (4 + gain), high.* (4 + gain) | - | MORE | low: bands is 1 band or 2/3 as today; high: same table conditions as now | - | rows hidden, not dimmed |
| crossover_low, crossover_high, limit_latency_comp | - | MORE | crossover only when that band exists; latency comp only in limit mode 1 | - | - |

*Delay* (6 of 14, 7 when the style uses the second time; G1 [STYLE, SYNC, TIME/DIV, FEEDBACK], G2 [FILTER CUTOFF, MIX])

| control | Y | MORE | hidden unless | read-only | note |
|---|---|---|---|---|---|
| style, sync, time/division, feedback, filter_cutoff, mix | Y | - | time/division pair by sync | - | - |
| time2 / division2 | - | MORE (default view only when style is stereo-time, **unverified** which styles) | style uses a second time | - | currently always shown in the TIME row |
| level, hq, filter_spread, offset, offset_left | - | MORE | offset_left only if offset != 0 (**unverified**) | - | - |

*Distortion* (5 to 7 of 9; G1 [TYPE, DRIVE, MIX, LEVEL], G2 [FILTER ORDER, CUTOFF, RESONANCE])

| control | Y | MORE | hidden unless | read-only | note |
|---|---|---|---|---|---|
| type, drive, mix, level, filter_order | Y | - | - | - | - |
| filter_cutoff, filter_resonance | Y | - | filter_order is 1 or 2 (as the live rule today) | - | hidden instead of dim |
| filter_blend, filter_keytrack | - | MORE | filter_order is 1 or 2 | - | - |

*EQ* (7 of 18; G1 [LOW CUTOFF, LOW GAIN, MID CUTOFF, MID GAIN], G2 [HIGH CUTOFF, HIGH GAIN, LEVEL])

| control | Y | MORE | hidden unless | read-only | note |
|---|---|---|---|---|---|
| low/mid/high cutoff + gain, level | Y | - | - | - | the curve display shows the rest |
| low/mid/high mode, low/mid/high resonance | - | MORE | - | - | mode = shelf/peak/cut kind |
| mid2.mode (+ cutoff, gain, resonance) | - | MORE | mid2 cutoff/gain/res only when mid2.mode is not Off | - | - |
| law | - | MORE | - | - | - |

*FX Filter* (6 to 8 of 18; G1 [MODEL, STYLE, CUTOFF, RESONANCE], G2 [DRIVE, MIX, model field 1, model field 2])

| control | Y | MORE | hidden unless | read-only | note |
|---|---|---|---|---|---|
| model, style, cutoff, resonance, drive, mix | Y | - | - | - | - |
| comb_offset | Y | - | model = Comb (id 6) | - | takes the model-field cell |
| formant.x, formant.y | Y | - | model = Formant (id 5) | - | cells 1 and 2 of the model field |
| formant.resonance, formant.spread, formant.transpose | - | MORE | model = Formant | - | row "FORMANT" |
| blend, keytrack, clean_mode, var, pan, level | - | MORE | - | - | - |

*Flanger* (6 of 9; G1 [RATE/DIV, SYNC, DEPTH, CENTER], G2 [FEEDBACK, MIX]); MORE: phase_offset, level. *Phaser* (6 of 11; same shape: G1 [RATE/DIV, SYNC, DEPTH, CENTER], G2 [FEEDBACK, MIX]); MORE: depth2, blend, phase_offset, level.

| control | Y | MORE | hidden unless | read-only | note |
|---|---|---|---|---|---|
| flanger/phaser rate-division, sync, depth, center, feedback, mix | Y | - | pair by sync | - | - |
| flanger phase_offset, level; phaser depth2, blend, phase_offset, level | - | MORE | - | - | - |

*Reverb* (6 to 8 of 21; G1 [MIX, TYPE, SIZE, DECAY], G2 [PREDELAY, WIDTH, ROOM, FEEDBACK])

| control | Y | MORE | hidden unless | read-only | note |
|---|---|---|---|---|---|
| mix, type, size, decay, predelay, width | Y | - | - | - | - |
| room | Y | - | type = 4 | - | existing live rule |
| feedback | Y | - | type = 4 or 5 | - | existing live rule |
| er_size, diff_a, diff_b, chorus_amount, chorus_rate, pre_low_cut, pre_high_cut | - | MORE | - | - | - |
| low/high_shelf cutoff + gain (4) | - | MORE | - | - | DAMPING row |
| level + force_enable | - | MORE | - | - | safe list: FORCE ON moves into the OUTPUT row (see K) |

*Hyper/Dimension* (6 of 8; G1 [RATE, UNISON, DETUNE, DIM SIZE], G2 [DIM MIX, MIX]); MORE: retrig, level.

*Utility* (6 to 7 of 10; G1 [WIDTH, PAN, LPF, HPF], G2 [MONO BASS, MONO FREQ, MIX])

| control | Y | MORE | hidden unless | read-only | note |
|---|---|---|---|---|---|
| width, pan, lpf, hpf, mono_bass, mix | Y | - | - | - | - |
| mono_freq | Y | - | mono_bass = on | - | hidden instead of dim |
| polarity_l, polarity_r, level | - | MORE | - | - | - |

*Bode* (6 of 14; G1 [SHIFT, RANGE, DIR, BALANCE], G2 [FEED, MIX])

| control | Y | MORE | hidden unless | read-only | note |
|---|---|---|---|---|---|
| shift, range, dir, balance, feed, mix | Y | - | - | - | - |
| delay/division, sync, blur, width, mono_input, retrig, level | - | MORE | division only in Tempo sync, delay only in Free | - | - |

*Convolve* (7 of 14; G1 [IR, SIZE, TONE, PRE-DELAY], G2 [ATTACK, DECAY, MIX])

| control | Y | MORE | hidden unless | read-only | note |
|---|---|---|---|---|---|
| ir, size, tone, pre-delay (pair by sync), attack, decay, mix | Y | - | pre-delay/division by sync | - | sync switch itself is in MORE (free default) |
| sync, phi_min, damp, ir_gain, law, level | - | MORE | - | - | - |

*Splitters* (L/H 4, L/M/H 6, M/S 3): all controls stay (already at or below 8), no MORE; the Q groups stay as today (G1 [SPLIT FREQ, LOW BYPASS, HIGH BYPASS, LEVEL]; L/M/H G1 [LOW SPLIT, HIGH SPLIT, LEVEL], G2 [LOW, MID, HIGH BYPASS]; M/S G1 [MID BYPASS, SIDE BYPASS, LEVEL]). Level of an effect stays editable here (its home is the effect, not the Mixer).

**A.4 FX page chrome (not per effect).** Chain bar, ADD, gallery, rack switch and band racks: unchanged. One-home rule: the FX page has no level/pan/destination of a module (the Bus/Main level lives on the Mixer); effect `level` is an effect parameter and stays. Disabled-block: each slot card's power icon is the effect's switch; when off, the editor and display of that slot are grey and take no touch (Q members of that slot are not published).

**A.5 Impact of the FX pilot (one owner per file).**

| File | Change | Owner (proposed) |
|---|---|---|
| `shell-mpc/ui/fx_layout.h` | add the default/MORE split to `Layout` (e.g. `defaultRows`), re-order the 13 tables per A.3 (rows of four = Q groups), header rule 2 -> hide, reverb OUTPUT row (U10), `kMaxPageRows` check | FX table owner |
| `shell-mpc/ui/fx_slot_editor.h` | MORE/LESS button instead of PAGE stepper (lines 511-527), default view = first rows, hide-not-dim (`numVis_`, lines ~92-94, 292, 340), disabled state | same FX owner |
| `shell-mpc/overlay/fx_ui.h` | Q publish for the default rows / MORE rows, MORE touch id, disabled bind to the slot power | same FX owner |
| `shell-mpc/ui/fx_screen.h`, `fx_display.h`, `fx_gallery.h`, `fx_chain_bar.h` | display heights stay; slot power is green ON / red OFF and the editor keeps navigation while disabled | same FX owner |
| tests: `shell-mpc/ui/tests/ui_fx_tables_host.cpp`, `ui_fx_editor_host.cpp`, `shell-mpc/overlay/tests/mixfx_ui_host.cpp` | counts per effect (at most 8 default cells), MORE reaches every key of the table, hidden-not-dim, FORCE ON row | FX owner |
| `shell-mpc/overlay/tests/qslots_host.cpp` | FX row of the published list (default groups, MORE groups) | shared test owner (single owner for this file across all steps) |
| docs: `docs/ui-map/fx.md`, `docs/MPC.md` (Q groups table row "FX"), `docs/ui-design/08-effects.md`, `docs/ui-design/18-FX-INSTANCES.md` | new default/MORE structure | doc owner |
| skin: none (FX tab is a plugin-drawn page; Q proxies unchanged); `scripts/overlay-skin/mpc-ui-pages-overlay.json` untouched | - | - |
| gallery / measurement | before/after pictures of all 16 kinds, default and MORE; overlay thread CPU before/after, Key 61 and XL | runner (single, buildslot rules) |

**FX pilot: implemented (2026-11-21, branch `agy/g128-fx-pilot`).** What it is and where: `docs/ui-map/fx-pilot/README.md` (cells before / after, MORE contents, before / after pictures of all 16 kinds), `docs/MPC.md` "FX editors: one table per effect". Changes against the plan above:
- `Layout::defaultRows` is the split (A.1); the PAGE n / m stepper is one MORE / LESS button (`FxSlotEditor::more`, the same touch id as the old stepper); no MORE button when nothing live is behind it. Rule 2 of the header is "always hide": the live cells of a row close up from the left (a hidden cell in the middle of a row does not leave a gap), so a Q group is always the live controls from the first slot.
- The tables are re-cut exactly per A.3 (default cells: Chorus 6, Compressor 8, Delay 6 + the second time, Distortion 5 + 2 with the filter on, EQ 7, FX Filter 6 + 2 model cells, Flanger 6, Phaser 6, Reverb 6 + 2 algorithm cells, Hyper 6, Utility 6 + the mono frequency, Bode 6, Convolve 7; the splitters unchanged). Compressor keeps LEVEL in the default view (8, the `*` cell was not dropped). Most effects now have `rowsPerPage` 2 (the default view has two rows); EQ and FX Filter 3 (their MORE has three rows under the display).
- A MORE view with more rows than the table's page (only the Compressor: 4 rows) gives the display up for four rows; every other MORE view keeps the display.
- U10 is done: the Reverb's FORCE ON is in the OUTPUT row (MORE) with LEVEL.
- UNVERIFIED items resolved by reading the code: (1) Delay second time: the engine reads `time2` / `division2` in every style but Mono (`core/engine-own/fx/fx_effects.h`, `Delay::process`: Mono uses the left time for both sides; Stereo, Ping pong, Mid ping pong and Tap->Delay use the right time), so the second time is in the default view in those four styles and hidden in Mono (a new `Live` second condition: sync AND style); it used to be shown always, also in Mono. (2) `delay.offset_left`: the engine multiplies the left time by it independently of `delay.offset`, so it is NOT hidden when the offset is 0; both stay in MORE. (3) `chorus.delay2`: the engine uses delay 2 for every voice count (every voice is a pair of two lines), so it is NOT hidden for one voice; it stays in MORE.
- Compressor, second cut after review: the default view shows MODE, THRESHOLD and RATIO of ONE selected band plus a view-local BAND selector (A.3 listed MID LOWER THRESHOLD / RATIO, which showed one band's numbers in Multiband); the selector is a cell (counts in the 8), is not a parameter and has no Q slot; LEVEL moved behind MORE because MODE is character-defining (the Limit mode drops the upper ratio, Serum mode is a peak compressor). Details in `docs/ui-map/fx-pilot/README.md`.
- The disabled-block rule is now exercised by OFF→ON host regressions for oscillator, filter and FX editor; the parent composites stay enabled so POWER and normal page navigation remain reachable, while disabled Q members reject turns. The device owner still measures overlay cost; no hardware claim is made here. `q_publish.h` and `qslots_host.cpp` (the router) need no change: disabled members remain published but cannot emit writes.

**Mixer + Master: implemented (2026-11-21, branch `work/p-mixer-master`).** What it is and where: `docs/ui-map/mixer-master-bold-cut/README.md` (control | before | after | where, before / after pictures), `docs/MPC.md` ("Mixer and FX", "Master and Voice", Q groups table), `docs/ui-map/mixer.md`, `master.md`. Changes against the plan above:
- Mixer (F): the dead OUT button of the MASTER strip is hidden (U9; 50 controls instead of 51); the routing bar chips (U8) became plain labels without the outline (the "visually distinguish" half of the recommendation, because "make tappable" would be a new feature and F says unchanged unless the owner adds it; if the owner wants them tappable it is a separate decision). Level, pan, destination and sends stay on the strips (the Mixer is the home). No disabled-block rule (separate item).
- Master (G, K "Master LINK"): LINK is a touch-only toggle in PITCH (Q4), the group LINK is removed (MAIN 5 groups, the page 9 instead of 10). The hide rules: BEND DOWN hidden while LINK is on; MPE BEND and BEND TO X hidden while MPE is off. The Voice items (M8) were not moved (judgement, open question below). LEGATO label fix (U11) is on the Env page, not Master: not part of this step.
- UNVERIFIED items resolved by reading the code: (1) `master.bend_down_link` (`own_engine.cpp`, `handleEvent` PitchBend): on = the down range is the UP range (`master.bend_range`) for every channel; `master.bend_down_range` is read only while LINK is off, so with LINK on BEND DOWN is not applicable and is hidden (and BEND UP is "BEND", both directions). With MPE on a member channel uses `master.mpe.bend_range` in both directions regardless of LINK. (2) "MPE sub-fields only when MPE is on": only partly true. `mpe.bend_range` and `mpe.bend_to_x` are read only when `mpe.on` and the event is on a member channel (hidden while MPE is off); `mpe.xyz_macro`, `mpe.y_modwheel` and `mpe.y_bipolar` are applied in `applyAxes` from CLAP note expressions without MPE too, so they are always shown.
- Q groups: hidden controls are no members; a group closes up from slot 1 (`MasterUi::publishQGroup` reads the live members of the screen's groups, no key table any more). LINK has no Q slot, so `coverage` for `master.bend_down_link` is "control yes, Q no, by design" (`mastervoice_ui_host`, `testFullCoverage`).
- Not done (not part of this step or the owner's decision): the overlay thread cost before / after (the coordinator's device step), the disabled-block rule, the skin generator (a separate worker), the Voice items to Master -> VOICES (M8), PAD % hidden while PAD PRESS is off (the spec lists PAD PRESS / PAD % as unchanged; the engine side of the value is a shell setting kept while the layer is off; the owner decides).

**Env / Filter / LFO, steps 0 and 1 implemented (2026-11-21, branch `work/step01-selector`).** Master: PAD % is hidden while PAD PRESS is off (hide what does not apply; `docs/ui-map/mixer-master-bold-cut/README.md`). Selector component `UsedSelector` and the chunk-only view state `mpcTabView` (rnd, filter): `docs/ui-map/env-filter-lfo-plan.md` "Steps 0 and 1: implemented", `docs/MPC.md` "Tab view state". The Env / LFO pages are not changed yet (steps 2 and 3).

**Env + LFO tabs, steps 2 and 3 implemented (2026-11-21, branch `work/step23-env-lfo`).** What it is and where: `docs/ui-map/env-lfo-bold-cut/README.md` (control | before | after | where, before / after pictures), the LFO recount and the decisions in `docs/ui-map/env-filter-lfo-plan.md` ("Steps 2 and 3: implemented"). Page list 22 -> 16.
- Env (C): ONE tab, selector = `UsedSelector` over ENV 1 - 6 (shared `Play Env`), U11 done (label RETRIG; the engine meaning checked), U13 done; 13 controls before and after.
- LFO (D, K): ONE tab, selector over LFO 1 - 10 and R1 - R4 with the `+` list; U1 - U3 done (the dead steppers and `InstanceBar` are deleted); U14 done (HOST is a touch-only toggle inside TIMING, no Q slot, TIMING frameless); EDIT only for the drawn shape (the DRAWN cell starts from the shape shown, so nothing is lost); the recount: standard Hz 16 (17 before), drawn 17, BPM 18 / 19, KEY 17, random 7 / 9 / 8 (the "about 28" of D counted the dead steppers and the cells; the "about 14" for the random LFO counted hidden ones).
- Question 2 of M (LFO 1-4 one tab) is answered yes (owner 2026-11-21) and done; question 4 (the toggles moved into full groups) is done for HOST: no Q slot.
- Not done: the Play page's own selectors keep all six / fourteen entries (`TODO.md`); the Filter merge (step 4: done below); the device check.

**Filter tab, step 4 implemented (2026-11-21, branch `work/step4-filter`).** ONE tab `Filter` with the four states F1 | F2 | NOTE | VELO (`mpcTabView.filter`), page list 16 -> 15; what it is and where: `docs/ui-map/filter-bold-cut/README.md` (control | before | after | where, pictures), the decisions in `docs/ui-map/env-filter-lfo-plan.md` ("Step 4: implemented"), the page in `docs/ui-map/filter.md`.
- E: F1+F2 is a view-only toggle (the controls edit the selected filter); COMB only for the Comb model (U4), VAR / CLEAN hidden with the Formant model, the FORMANT block and its RESONANCE (in the TRACK block, the place of VAR) only for the Formant model; the OUTPUT popup (U5) and the model list button (D10) and the dead OUT button removed; LEVEL, PAN, DEST, BUS 1, BUS 2 read-only (Q3: BLEND and ROUTING stay editable); Q groups MAIN, TRACK, OUT = [BLEND, ROUTING, MODEL, STYLE], FORMANT. Controls 19 -> 15 (24 with the popup), Formant 24 -> 18.
- NOTE / VELO: the step 5 editors embedded (USED BY, TARGETS -> `ModTargetsPage`, the two legato keys on Q2, the live marker, the axis C-2 ... G8).
- Not done: the Mixer chips (held), the disabled-block rule, the Osc / Play pages; the device check (`TODO.md`).
- Not done: the Filter merge (step 4); the device check.

**Osc + Play implemented (2026-11-21, branch `work/step7-osc-play`).** What it is and where: `docs/ui-map/osc-play-bold-cut/README.md` (counts, control | before | after | where, before / after pictures), `docs/MPC.md` ("Oscillator UI", "Play page", Q groups table), `docs/ui-map/osc.md`, `play.md`. Changes against B / B2 / B3 above:
- Default views: wavetable 12, granular 12, multisample 10, sample 10, spectral 9, sub 8 as listed; noise generated 9 and noise sample 11 (12 with LOOP on). SOUND and KIND of the noise are two parameters (`noise.type`, `noise.sample`), so the generated noise has one control more than the list; SOUND is hidden for a sample. Level, pan and destination are a read-only indicator (`OscMixIndicator`); the F1 / F2 balance (`osc1.filter_blend`) moved to MORE (question 3 of M answered by the build: the route popup is the Play page's shortcut only). The Q groups follow the default view: Basic = the top row [TYPE, SOURCE, position knob, OCT] (Sub [WAVEFORM, OCT, CRS, PHASE], Noise [KIND, SOUND, FILTER, STEREO]); the WARP group of the Q-Link mapping is kept.
- The Tune MORE page is the single MORE (`oscspec::kMore`, one page for the five types with hidden controls per type); the old options page is OPTIONS behind it (`oscspec::kOptions`, page 23). The page buttons (MAP, ENV, REGION, SLICE, FILTER, SCAN, GRAIN, WINDOW, UNISON) are on MORE. The dead bound-but-never-shown rows of the Sub and Noise bar presets are gone with the type bar.
- UNVERIFIED items resolved by reading the code: (1) a warp amount "only if its type != Off": hidden (the engine reads `warp.amount` only for a type that is not Off, `WarpSlot` already dimmed it); (2) `noise.bounce` is shown while LOOP is on: a guess from the name, not read in the engine (`TODO.md`).
- Play: the Env and LFO carousels and lists are the tabs' `UsedSelector` cells (`uibind::cellsFromSelector`), an unedited `+` pick falls back to the last used item when the TAB is left (`uibind::fallbackSelection`, `leftTab`), the 4th Q group's LFO MONO exists only for a standard LFO.
- Not done (not part of this step): the disabled-block rule, the Mixer peek panel, the skin generator, the Filter merge (step 4), the device check (`TODO.md`).

---

### B to J: PROPOSAL, pending the owner's yes after the FX pilot

#### New tab list (L, summarised here for the tables below)

22 pages in 9 function keys today (`docs/MPC.md` "Function keys and sub-tabs", `scripts/overlay-skin/mpc-ui-pages-overlay.json`). After merging Env 1-4 into one Env tab and Filter 1-2 into one Filter tab: **18 pages, still 9 function keys** (Env and Filter become keys with a single tab and no sub-tabs). Order: `Play` (Play, Macros); `Osc` (Osc A, Osc B, Osc C, Sub, Noise); `Env` (Env); `LFO` (LFO 1-4); `FX` (FX); `Mix` (Mixer); `Filter` (Filter); `Mod` (Mod); `Master` (Master, Voice). Selector placement: the Env tab keeps its existing in-page selector in the top bar (cells ENV 1 .. ENV 6: the page already addresses 6 envelopes, the skin only had 4 tabs, so ENV 5 and 6 become reachable from the tab too); the Filter tab keeps its CURVES selector (F1 | F2 | F1+F2) in the top bar, left of MODEL; choosing F1 or F2 selects which filter the whole page edits (**unverified**: what an edit does while F1+F2 is shown; check `filter_ui.h` before the step, proposal: F1+F2 is view-only, controls act on the last selected filter).

#### B. Osc A / Osc B / Osc C (one table per engine type; B and C identical with `osc2.` / `osc3.`)

Limit 8 to 12 default controls per type. Today about 74 per tab. Level, pan and destination are read-only here (home: Mixer); POWER stays (it is the block's switch). The Tune column's MORE button (`oscspec::kMore`) becomes the ONE MORE of the tab and takes the items moved from the bottom panel (**unverified** that `kMore` has room: it holds 20+ parameters already per `osc.md`; if not, a second detail page "DETAIL").

Wavetable (type 0): default 12.

| control | Y | MORE | hidden unless | read-only | note |
|---|---|---|---|---|---|
| POWER (`osc1.on`) | Y | - | - | - | disables the rest of the block |
| TYPE, TABLE | Y | - | - | - | - |
| WT POS (`osc1.frame`) | Y | - | - | - | - |
| UNISON voices, DETUNE | Y | - | - | - | gear/detail page stays one tap away |
| WARP 1 TYPE + AMOUNT, WARP 2 TYPE + AMOUNT | Y | - | WARP n amount only if its type != Off (**unverified**) | - | - |
| OCT (transpose), FIN (tune) | Y | - | - | - | - |
| BLEND, WARP 1/2 PHASE, SEM, CRS, TRACK, PHASE, RAND, MAP, FRAME read-out, unison gear | - | MORE | - | - | FRAME is a read-out, not a control |
| LEVEL, PAN, ROUTE (destination, bus sends) | - | - | - | R | indicator chips only; BLEND of F1/F2 stays in the ROUTE popup (see question 3) |

Q groups (4 each): G1 [TYPE, TABLE, WT POS, OCT]; G2 [UNISON, DETUNE, FIN, -]; G3 [WARP 1 TYPE, WARP 1 AMOUNT, WARP 2 TYPE, WARP 2 AMOUNT].

Multisample (type 4): default 10: POWER, TYPE, SOURCE (content), TIMBRE, UNISON, DETUNE, WARP 1 TYPE + AMOUNT, OCT, FIN. MORE: ENV button, VEL TRACK toggle + amount (SIMPLIFY section 4: into the multisample envelope detail page), BLEND, warp phases, SEM/CRS/TRACK, MAP. Q: G1 [TYPE, SOURCE, TIMBRE, OCT]; G2 [UNISON, DETUNE, FIN, -]; G3 [WARP 1 TYPE, WARP 1 AMOUNT, -, -].

Sample (type 1): default 10: POWER, TYPE, SOURCE, SCAN, UNISON, DETUNE, WARP 1 TYPE + AMOUNT, OCT, FIN. MORE: REGION, SLICE (spec pages), BLEND, warp 2, warp phases, SEM/CRS/TRACK. Q: G1 [TYPE, SOURCE, SCAN, OCT]; G2 [UNISON, DETUNE, FIN, -]; G3 [WARP 1 TYPE, WARP 1 AMOUNT, -, -].

Granular (type 2): default 12: POWER, TYPE, SOURCE, SCAN, DENS, LENGTH, WARP (type), OFFSET, DIR, PITCH, OCT, FIN. MORE: LEN RAND, PAN RAND, LVL RAND, AMOUNT (window), LOOP, GRAIN page, UNISON, SEM/CRS/TRACK, BLEND. Q: G1 [SCAN, DENS, LENGTH, WARP]; G2 [OFFSET, DIR, PITCH, OCT]; G3 [TYPE, SOURCE, FIN, -].

Spectral (type 3): default 9: POWER, TYPE, SOURCE, POSITION, UNISON, DETUNE, WARP 1 TYPE + AMOUNT, OCT. MORE: FILTER, SCAN pages, BLEND, warp 2, FIN, SEM/CRS/TRACK. Q: G1 [TYPE, SOURCE, POSITION, OCT]; G2 [UNISON, DETUNE, -, -]; G3 [WARP 1 TYPE, WARP 1 AMOUNT, -, -].

Common to the five types: the TypeBar (FRAME read-out, PHASE, RAND, MAP/GRAIN/REGION/SLICE/FILTER/SCAN buttons, ENV, VEL TRACK) loses its default-view place except where listed Y; its content moves to MORE or its existing detail page. Hidden unless: per-type controls are drawn only for the active type (already the case).
Default-view count versus limit: Wavetable 12/12, Granular 12/12, Multisample 10, Sample 10, Spectral 9. Visible per tab about 74 -> 9 to 12 (plus the display).

#### B2. Sub (limit 8 to 12; default 8)

| control | Y | MORE | hidden unless | read-only | note |
|---|---|---|---|---|---|
| POWER (`sub.on`), WAVEFORM (6 pictograms), OCT (`sub.octave`), CRS (`sub.tune`), PHASE (`sub.phase`), ENV 1, MONO, FREE RUN | Y | - | - | - | 8 |
| DEST, BLEND (`sub.filter_blend`), BUS 1, BUS 2 | - | - | - | R | ROUTING block becomes read-only indicator; BLEND: see question 3 |
| LEVEL, PAN | - | - | - | R | home Mixer |

Q: G1 [WAVEFORM, OCT, CRS, PHASE]; G2 [ENV 1, MONO, FREE RUN, -]. (Today: PITCH & MODE, PHASE & ROUTE, ROUTING.)

#### B3. Noise (limit 8 to 12)

Generated mode (default 7): POWER, SOUND/KIND, FILTER, PITCH, FINE, KEY TRACK, STEREO, ENV 1 (8 counting ENV 1). Q: G1 [SOUND/KIND, FILTER, PITCH, FINE]; G2 [KEY TRACK, STEREO, ENV 1, -]. Hidden unless sample mode: START, RANDOM, LOOP, BOUNCE, source. Sample mode (default 11): POWER, SOURCE, FILTER, PITCH, FINE, KEY TRACK, START, STEREO, ENV 1, LOOP, RANDOM; BOUNCE hidden unless LOOP is on (**unverified** that bounce is meaningful only when looping); Q: G1 [SOURCE, FILTER, PITCH, FINE]; G2 [KEY TRACK, START, STEREO, ENV 1]; G3 [RANDOM, LOOP, BOUNCE, -]. Read-only: LEVEL, PAN, DEST (Mixer). MORE: none needed (<= 12) except bus sends/blend as in B.

#### C. Env (merged: Env 1-4 -> ONE Env tab, in-page selector ENV 1 .. ENV 6)

New in-page structure: top bar = [CLOSE (full-screen only)] [ENV 1..6 selector] [destination tags] [TARGETS]; graph; bottom band three Q groups. Default view (14 touch controls): selector, TARGETS, LOCKED/FREE, graph handles (the 5 time handles + 3 curve handles are the same parameters as the knobs), 4 + 3 + 2 knobs/toggles.

| control | Y | MORE | hidden unless | read-only | note |
|---|---|---|---|---|---|
| selector ENV 1..6 | Y | - | - | - | merges the 3 sub-tabs |
| ATTACK, DECAY, SUSTAIN, RELEASE | Y | - | - | - | Play compact ENV keeps its copy (intentional) |
| A CURVE, D CURVE, R CURVE | Y | - | - | - | - |
| DELAY, HOLD | Y | - | - | - | - |
| BEATS | Y | - | - | - | - |
| LEGATO (`legato_inverted`) | Y | - | - | - | safe list: label fix (K) |
| TARGETS, LOCKED/FREE | Y | - | - | - | - |

Q groups unchanged: ADSR [ATTACK, DECAY, SUSTAIN, RELEASE]; SHAPE [A CURVE, D CURVE, R CURVE, -]; TIME [DELAY, HOLD, BEATS, LEGATO]. No module switch (envelopes have no `*.on`), no disabled state.

#### D. LFO 1-4 (4 sub-tabs stay, the merge was not part of the decision; question 2). Same table for LFO 1 to 4 and for std vs random.

| control | Y | MORE | hidden unless | read-only | note |
|---|---|---|---|---|---|
| instance cells 1..10, R1..R4 | Y | - | - | - | - |
| shape picker, RATE / DIVISION, RISE, SMOOTH (+ switch) | Y | - | RISE, SMOOTH, shape picker only in a standard LFO | - | - |
| TIMING: HZ/BPM/KEY, PHASE, DELAY, MODE, HOST | Y | - | HOST only in a standard LFO | - | HOST moves in (safe list) |
| TRIP, DOT | Y | - | sync = BPM | - | hidden instead of shown always (section 4) |
| KEY TRANSPOSE, KEY TUNE | Y | - | sync = KEY | - | - |
| MOTION: DIRECTION, SWING, STEREO (knob), MONO | Y | - | standard LFO only (MONO is already hidden when random, `lfo_screen.h:196`); STEREO is a toggle in random | - | - |
| EDIT (shape editor) | Y | - | shape = DRAWN | - | on Play compact also |
| random LFO: style grid, RATE/DIV, HZ/BPM/KEY, STEREO toggle, MODE | Y | - | random only | - | PHASE, DELAY, DIRECTION, SWING, RISE, SMOOTH, HOST, shape picker hidden for the random LFO (**unverified** which of these are drawn today for random: audit `lfo_screen.h` `configure(random)` lines 40-50 and 189-196) |
| EDIT page internals (POINT, GRID, ...) | - | existing detail page | - | - | unchanged |

Q groups: standard: RATE [SHAPE, RATE/DIV, RISE, SMOOTH]; TIMING [SYNC, PHASE, DELAY, MODE]; MOTION [DIRECTION, SWING, STEREO, MONO]; KEY FOLLOW [TRANSPOSE, TUNE] only in KEY sync; HOST becomes touch-only in the TIMING block (question 4); the SHAPE group [HOST, SMOOTH switch] shrinks to [SMOOTH switch] and joins RATE as touch (or stays one 2-member group). Random: RANDOM [STYLE, RATE/DIV, SYNC, STEREO]; TRIGGER [MODE]; KEY FOLLOW only in KEY sync. Default view counts about 40 -> about 28 (standard), about 14 (random).

#### E. Filter (merged: Filter 1-2 -> ONE Filter tab, CURVES selector F1 | F2 | F1+F2)

| control | Y | MORE | hidden unless | read-only | note |
|---|---|---|---|---|---|
| POWER (of the selected filter) | Y | - | - | - | disables the block |
| CURVES selector | Y | - | - | - | replaces the Filter 2 tab |
| MODEL (stepper, centre tap opens list), STYLE, CLEAN | Y | - | - | - | modelList arrow removed (K) |
| OUTPUT button | Y | - | - | - | popup keeps DEST? no: DEST is a Mixer parameter (read-only here); popup keeps BUS sends read-only; see question 3 (recommend: popup removed, BLEND and ROUTING stay in the OUT block) |
| CUTOFF, RESO, DRIVE, MIX (MAIN) | Y | - | - | - | display puck stays |
| KEYTRACK, ENV 1, VAR (TRACK) | Y | - | - | - | - |
| COMB | Y | - | model = Comb (id 6) | - | takes TRACK Q4 |
| FORMANT X, Y, TRANSPOSE, SPREAD | Y | - | model = Formant (id 5) | - | FORMANT block already conditional |
| FORMANT RESONANCE | Y | - | model = Formant | - | add it to the FORMANT group or drop the TRANSPOSE/SPREAD pairing (SIMPLIFY section 4) |
| BLEND, ROUTING (`filter.routing`) | Y | - | - | - | stay editable: they are filter-pair parameters with no Mixer home |
| LEVEL, PAN, DEST, BUS 1, BUS 2 | - | - | - | R | Mixer strips F1 / F2 are the home |

Q groups: MAIN [CUTOFF, RESO, DRIVE, MIX]; TRACK [KEYTRACK, ENV 1, VAR, COMB (Comb only)]; OUT [BLEND, ROUTING, -, -]; FORMANT [X, Y, TRANSPOSE, SPREAD] (Formant only). Default controls about 47 -> about 17.

#### F. Mixer (the home of level / pan / destination; no module is read-only here)

| control | Y | MORE | hidden unless | read-only | note |
|---|---|---|---|---|---|
| per strip: on, route, LEVEL, PAN, B1, B2 (Sub, A, B, C, Noise, F1, F2) | Y | - | - | - | one strip = one Q group (unchanged) |
| BUS1, BUS2: power, route, LEVEL | Y | - | - | - | - |
| MASTER: power, LEVEL | Y | - | - | - | - |
| MASTER OUT button | - | removed | - | - | dead button (K) |
| routing bar chips | Y | - | - | - | tappable or plain graphics (SIMPLIFY U8, not in the owner list: unchanged unless the owner adds it) |

Disabled-block: a strip whose source is off (`*.on` = 0) is grey except its on switch. Control count about 50 -> about 49.

#### G. Master (side MAIN / TUNING & MPE; Master Level lives on the Mixer MASTER strip but the OUTPUT block is its page: kept, marked "intentional shortcut" like Play)

| control | Y | MORE | hidden unless | read-only | note |
|---|---|---|---|---|---|
| VOLUME, SPREAD, STEREO, VOICE OVERRIDE; LIMIT; NO SMOOTHING, OVERSAMPLE 4X | Y | - | - | - | unchanged |
| TUNE, TRANSPOSE, BEND UP, BEND DOWN, LINK | Y | - | BEND DOWN hidden while LINK is on (it follows BEND UP; **unverified** semantics, check `master.bend_down_link`) | - | LINK moves into PITCH (K) |
| POLY, LEGATO, POLY LEG, PHASE LEG; VEL TRACK, SAME NOTE, STEAL; PAD PRESS/PAD %; GLIDE group | Y | - | - | - | unchanged |
| tuning/MPE side | Y | - | MPE sub-fields only when MPE is on (**unverified**) | - | - |

Q groups main: OUTPUT [VOLUME, SPREAD, STEREO, VOICE OVERRIDE]; PITCH [TUNE, TRANSPOSE, BEND UP, BEND DOWN]; VOICES [POLY, LEGATO, POLY LEG, PHASE LEG]; VOICE OPTS [VEL TRACK, SAME NOTE, STEAL, -]; GLIDE [TIME, SLOPE, ALWAYS, SCALED] (5 groups instead of 6). Side 2 unchanged (SCALE, TUNING, MPE, MPE Y).

#### H. Voice, I. Mod, J. Macros, Play

| page | change in the bold cut | note |
|---|---|---|
| Voice (59) | none (SIMPLIFY M8 not in the owner's list); only the disabled rule does not apply (no module switch) | Q groups unchanged: STEP, RANDOM, RAND 10x, SCALING, TARGETS, NOISE |
| Mod | none (slot rows, slot detail unchanged) | U12 and the source board toggle are not in the owner's list |
| Macros | none; Spectrum strip (U6) stays unless the owner adds it | MACROS 1-4, 5-8 unchanged |
| Play | compact OSC: level, pan, on, destination, type, source stay (intentional shortcuts, owner (e)); compact LFO: EDIT only when shape = DRAWN, TRIP/DOT only in BPM sync, random LFO shows only its own controls; the compact ENV/LFO copies stay; disabled rule: Sub/Noise/Osc inactive = wave, zoom button, controls grey and untouchable | Q groups unchanged: Osc [Basic], Env [ADSR], LFO [RATE] |

---

### K. Safe list: where each change goes (owner (f); all invisible or label-only, no engine change)

| # | Item | File / line (checked 2026-11-21) | Change |
|---|---|---|---|
| U1 | dead MONO `monoTop` of the LFO full screen | `shell-mpc/ui/lfo_screen.h:135` (decl), `:161` (setup), `:177` (addKid), `:192` (`show (monoTop, false)`), `:357` (place) | delete the member and its five uses |
| U2 | dead `style` stepper | `lfo_screen.h:47` (`show`), `:142` (decl, per SIMPLIFY, not re-read), `:193` | delete |
| U3 | dead `shape` / `sync` steppers | `lfo_screen.h:46-47, 196, :142`; the picker and `syncSwitch.mode` stay | delete |
| U13 | dead `T::Field` / NumberField path in `EnvUi` | `shell-mpc/overlay/env_ui.h:59` (enum value), `:68` (slot comment), `:423-437` (pull), `:575-578`, `:772`, `:987` (other `T::Field` uses) | delete together; confirm by test that `bind()` never gets `T::Field` (`env.md`) |
| U5 | filter OUTPUT popup LEVEL / PAN | `shell-mpc/overlay/filter_ui.h:286-287` (`route_.level/pan.setId`), `shell-mpc/ui/filter_display.h:554-555` (`Knob level, pan`) | remove both knobs, popup keeps DEST + BUS 1 + BUS 2 (read-only after the one-home rule) |
| D10 | `modelList` button | `filter_display.h:771` (decl), `:812` (addKid), `:910` (place); `filter_ui.h:257` (`setId (kModelList)`) and its handler | remove the 56 px button; stepper centre tap already opens the list |
| U9 | dead OUT button on the Mixer MASTER strip | `shell-mpc/overlay/mixer_ui.h:156-157` (`setLabel ("OUT")`, `setEnabled (false)`) | hide it (strip layout in `shell-mpc/ui/mixer_strip.h`) |
| U11 | LEGATO label on the inverted parameter | `shell-mpc/ui/env_screen.h:175, 190`; binding `shell-mpc/overlay/env_ui.h:362` (`legato_inverted`) | rename the label to RETRIGGER (check the meaning of `legato_inverted` first, **unverified**), tests in `env_ui_host.cpp` |
| U10 | reverb FORCE ON | `shell-mpc/ui/fx_layout.h:168` (row "TIER") | into a row "OUTPUT" with LEVEL (MORE view of Reverb, A.3); part of the FX pilot |
| U14 | LFO HOST into TIMING | `lfo_screen.h:138` (decl), `:165`, `:178`, `:192`, `:326` (Q member of SHAPE), `:372` (place); `shell-mpc/overlay/lfo_ui.h:505` (`kHostPick` bind) | draw HOST inside the TIMING block, drop it from the SHAPE group |
| - | Master LINK into PITCH | `shell-mpc/ui/voice_master.h:476` (group list), `:491` (label), `:562` (names), `:684` (layout comment: LINK is PITCH 2); `shell-mpc/overlay/master_ui.h:248`, `:482` | LINK as a toggle inside the PITCH block, group "LINK" removed |
| U4 | COMB only for the Comb model | `shell-mpc/overlay/filter_ui.h:316` (`kCombOffset`, `kModelComb` at `:66`), TRACK block in `filter_display.h` | show only when model = Comb (same as FORMANT) |

---

### L. Implementation order, impact per step (smallest first; one owner per file)

| Step | Scope | Files touched | Tests / docs / skin | Status |
|---|---|---|---|---|
| 1 | FX pilot (A) incl. U10 | `fx_layout.h`, `fx_slot_editor.h`, `fx_ui.h`, `fx_chain_bar.h` (disabled colour only) | `ui_fx_tables_host.cpp`, `ui_fx_editor_host.cpp`, `mixfx_ui_host.cpp`; `docs/ui-map/fx.md`, `docs/MPC.md`, `docs/ui-design/08-effects.md`; gallery + CPU cost | pilot, first |
| 2 | Mixer: OUT button (U9); strips become the home | `mixer_ui.h`, `mixer_strip.h` | `mixfx_ui_host.cpp`; `docs/ui-map/mixer.md` | proposal |
| 3 | Master: LINK into PITCH | `voice_master.h`, `master_ui.h` | `mastervoice_ui_host.cpp`; `master.md`; the Master row of `docs/MPC.md` Q groups | proposal |
| 4 | Env: ONE tab, selector 1-6, U11, U13 | `env_ui.h`, `env_screen.h`, `overlay_layout.json` (views env2-4 removed, env1 kept), `scripts/overlay-skin/mpc-ui-pages-overlay.json` (3 pages removed), `scripts/mpc-skin-extras/10_env.py`, `95_subtabs.py` | `env_ui_host.cpp`, `ui_env_host.cpp`, `scripts/tests/test_mpc_skin_overlay.py` (tab count 22 -> 19, Q-Links maps), `play_host.cpp` (Play ENV selector unaffected); `env.md`, `docs/MPC.md` (layout line "22 tabs in 9 function keys" and Env text near line 418) | proposal |
| 5 | Filter: ONE tab, curve selector, COMB (U4), U5, D10, one-home read-outs | `filter_ui.h`, `filter_display.h`, layout JSONs, `50_filter_overlay.py` (only for the variant), `95_subtabs.py` | `filter_ui_host.cpp`, skin test (19 -> 18 tabs); `filter.md`; `docs/ui-design/04-filters.md` | proposal |
| 6 | LFO: dead steppers (U1-U3), HOST into TIMING (U14), hide TRIP/DOT/KEY/random | `lfo_screen.h`, `lfo_ui.h`, `lfo_display.h`/`lfo_parts.h` as needed | `lfo_ui_host.cpp`, `lfo_live_host.cpp`; `lfo.md` | proposal |
| 7 | Osc: default views per type, read-only level/pan/dest, merged MORE | `osc_ui.h`, `osc_page.h`, `osc_spec_page.h`, `ui/screen.h`, `ui/panel.h`, `ui/composite.h` | `osc_ui_host.cpp`, `osc_page_host.cpp`, `ui_screen_host.cpp`; `osc.md`, `docs/MPC.md` (OscillatorFullScreen, panel presets) | proposal |
| 8 | Play: hide-what-does-not-apply on the compact views; shortcuts stay | `play_view.h`, `play_rows.h`, `play_osc.h` | `play_host.cpp`, `ui_screen_host.cpp`; `play.md` | proposal |
| X | Disabled-block rule (parallel to the pilot, owner: the coordinator, `TODO.md`) | `shell-mpc/ui/base.h`/`core.h` (central disabled state), then each page's `*.on` bind | page host tests (X.on = 0: touch ignored, disabled palette) | separate item |

Shared files that every step touches and therefore get ONE owner for the whole bold cut: `shell-mpc/overlay/tests/qslots_host.cpp`, `shell-mpc/overlay/q_publish.h`, `docs/MPC.md`, `docs/ui-map/OVERVIEW.md`, `shell-mpc/overlay/overlay_layout.json`, `scripts/overlay-skin/mpc-ui-pages-overlay.json`, `scripts/mpc-skin-extras/95_subtabs.py`. Every step needs the gallery before/after pictures and the owner's yes before the next one starts. Build/test only once per step on the merged tree through `buildslot.sh`.

### M. Open questions for the owner (max 5)

1. Does the "at most 8" for FX count the on/off and the MORE button? Recommendation: no; 8 editor cells plus the card's power icon and MORE (the cells marked `*` are the ones to drop if the owner wants a strict 8).
2. Should LFO 1-4 also become ONE tab (the LFO page already has an in-page selector 1-10 and R1-R4), like Env and Filter? Recommendation: yes, after the Env/Filter step has proved the pattern (LFO 1-4 -> 1 would give 15 pages instead of 18); not part of the pilot.
3. One home for bus sends and the F1/F2 BLEND: only level, pan and destination were named. Recommendation: bus sends and destination read-only on the module pages (Mixer is the only place), BLEND (osc F1/F2 balance) and the filter ROUTING stay editable on the module (no Mixer home), the ROUTE popup stays as the place of BLEND only.
4. Toggles moved into full 4-groups (LFO HOST into TIMING, Master LINK into PITCH) have no free Q slot. Recommendation: touch-only toggle inside the block (the Key 61 reaches it by touch), because an extra Q group for one toggle is what the cut removes.
5. MORE implementation: a second view of the same editor (the existing paging, Q groups follow it, a LESS button) or a popup? Recommendation: second view of the same editor on FX (zero new popup code, display stays visible); on Osc the existing Tune MORE page.

## Owner decisions on the tab merges (2026-11-21)

- Env 1-4 -> ONE Env tab (selector ENV 1-6), Filter 1-2 -> ONE Filter tab (curve selector F1 | F2 | F1+F2), and (owner "Igen", 2026-11-21) LFO 1-4 -> ONE LFO tab: 15 pages in 9 function keys after the three merges.
- LFO selector rule ("used + one free", like Serum 2 and Vital): the in-page selector shows only the LFOs IN USE plus ONE next free slot ("+"), in number order. An LFO counts as used when any mod slot or aux source uses it as its source, or it is the one being edited; LFO 7-10 appear after 6 is assigned; the random LFOs (R1..R4) appear when used or chosen from the "+"; an empty preset shows "1" and "+". The selection must survive a preset/state reload; hidden LFOs keep their parameters (nothing is deleted). The same "used + one free" rule applies to the Env selector of the merged Env tab (ENV 1-6) and to the Play page's LFO/Env selectors where consistent.
- Tests: the selector list for empty, partly used and all-used presets; the Q-Links follow the selected LFO. Gallery before/after, then the device check.
- Hide PAD % while PAD PRESS is off (Master; "hide what does not apply"); Voice SCALING/TARGETS stay on the Voice page; the Mixer routing chips stay labels (tappable = a new feature: owner's call).
- Mixer routing bar (owner "Legyen a koppintásra navigáció", 2026-11-21): the chips become tap-to-navigate and get the button look back (a short press feedback, NO parameter writes): FLT 1 / FLT 2 -> the Filter page with that filter selected (after the Filter merge: the curve selector set to F1 / F2); MAIN FX / BUS 1 / BUS 2 -> the FX page with that rack selected. Host test: each chip opens the right page and selection; gallery before/after. Goes with the Env/Filter merge step (the Filter target depends on it) or right after it.


## Env / Filter / LFO plan

See `docs/ui-map/env-filter-lfo-plan.md` (2026-11-21): the implementation plan for the Env / Filter / LFO tab merges (15 pages in 9 function keys), the "used + one free" selector component and its persistence, the target lists of the three merged tabs, the NOTE / VELO curve editors (the curves are content, not parameters), the Mixer chip navigation (blocked on a host-tab mechanism) and the open questions. PLAN only, for review before any code.
