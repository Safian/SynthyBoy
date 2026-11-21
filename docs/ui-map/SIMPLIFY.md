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
| D5 | All LFO params on Play compact vs LFO full-screen | Appear in both compact third and LFO full-screen | LFO full-screen | Compact third duplicates — already covered by rule: compact is the shortcut, full-screen is the authority | No removal needed; the duplicate table in lfo.md lists them for awareness. Play compact is the intentional shortcut. | — |
| D6 | `envN.rate`, `envN.division`, `envN.sync`, `envN.sync_type`, `envN.phase`, `envN.delay`, `envN.direction`, `envN.swing`, `envN.stereo`, `envN.mono`, `envN.fade_in`, `envN.smooth*`, `envN.keytrack*`, `envN.shape`, `envN.host` | Play compact ENV controls block vs Env full-screen | Env full-screen is the authority | **Remove from Play compact ENV** — these advanced timing/motion params are not shown in the compact view now either; they appear only in the full docs as "also on" full-screen. No action needed: they were never visible in Play compact in the first place. | — | — |
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
