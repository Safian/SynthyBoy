# Instruction & Knowledge Audit (2026-11-19)

> **Purpose & Scope**: A comprehensive, read-only audit of every instruction, knowledge, architecture, review, handoff, plan, and memory document read by AI agents working on the SynthyBoy MPC / VST project. The objective is to identify contradictions, stale rules, duplicated documents, divergent numbers, and false statements so that future passes can correct them.
> 
> **Single Source of Truth**: `docs/release/OUTPUT-LEVEL.md` on branch `work/loudness-fix`. Where any other document or past report disagrees with `OUTPUT-LEVEL.md`, `OUTPUT-LEVEL.md` wins conclusively.

---

## 1. Ground Truth Principles

All findings in this audit are evaluated against the authoritative facts established by the project owner and recorded in `docs/release/OUTPUT-LEVEL.md` (branch `work/loudness-fix`):

1. **Unity Gain Staging**: Default sound utilizes unity gain staging throughout the audio chain. The shell output stage make-up gain is strictly **0 dB** (`synthyboy::kMakeupGainDb = 0.0f`). Output levels live entirely within each preset's `master.volume`. Hidden make-up gains (+18 dB, +9 dB, +6 dB, -2.5 dB, -1.0 dB) are refuted and superseded.
2. **Limiter Default OFF**: The look-ahead peak limiter is **OFF by default** (`kOutputLimiterDefaultOn = false`) with **zero added latency** across all shells. The `LIMIT` toggle on the Master page is an opt-in user switch. Presets may only activate a limiter where internal drive/distortion necessitates it.
3. **Limiter Ceiling & Latency**: When active, the shared output stage limiter operates with a ceiling of **-0.5 dBFS** (0.94406) and 3 ms look-ahead (132 samples at 44.1 kHz, 144 samples at 48 kHz). Latency reporting follows the switch (0 samples when OFF).
4. **Stock Instrument Levelling**: Presets are calibrated to match the perceived and measured level of stock Akai MPC instruments (e.g. Hype, Sub Factory, Mini D, Solina, Fabric Piano). Peak-only fitting and the 'loudest 100 ms' ITU-R BS.1770 measure are refuted for decaying sounds (Keys, Plucks) because they obscure sustain.
5. **No Asterisk in Program Names**: The plugin never appends `" *"` to any program or preset name visible to the host (`work/no-asterisk`). The `programModified` dirty flag is removed.
6. **MPC XL Runs High Profile**: The MPC XL runs the `high` quality profile (`profile: high,voices=16,control=economy`), providing 16 voices, up to 16 unison voices, all effects, 4x warp oversampling, full-rate morph, and exact math. (The literal label `low,` in older logs was an artifact of `describeProfile`).
7. **Uniform Release Version**: The first public release version is strictly **0.1.0 build 1** across all packages, manifests, XML files, and content packs.

---

## 2. Detailed Audit Findings Grouped by File

Below, every audited file is listed under its respective category. For files with identified contradictions, a 5-column table specifies the exact file and line, the erroneous or stale statement, the contradictory fact, the verified truth, and the proposed fix.

### CLAUDE.md / AGENTS.md in repo

#### `AGENTS.md`

*Checked, nothing found.*

#### `shell-mpc/ui/AGENTS.md`

*Checked, nothing found.*

#### `template-mpc/AGENTS.md`

*Checked, nothing found.*

### docs/MPC.md

#### `docs/MPC.md`

| file:line | the statement | what contradicts it (file:line or measured fact) | which statement is true and why | proposed fix (one sentence) |
|---|---|---|---|---|
| `docs/MPC.md:107` | After the MPC application was restarted, the project (Sbtest) loaded with the preset name "Acid Saw Reso *" | docs/release/OUTPUT-LEVEL.md: A6 / docs/release/DEVICE-ROUND-FINDINGS-2.md:19 | No " *" in any program name the host sees (branch work/no-asterisk removed the mark) | Remove " *" from the program name description and document that preset names never append an asterisk. |
| `docs/MPC.md:746` | a make-up gain, synthyboy::kMakeupGainDb = -1.0 dB, the one value of every shell | docs/release/OUTPUT-LEVEL.md: A2 / docs/release/OUTPUT-LEVEL.md: C59 | Default sound = unity gain staging (shell make-up gain 0 dB); level lives in the preset master.volume | Update kMakeupGainDb default to 0.0 dB (unity gain staging) and mark -1.0 dB as superseded. |
| `docs/MPC.md:747` | a stereo-linked look-ahead peak limiter behind it (section "Output limiter"), on by default | docs/release/OUTPUT-LEVEL.md: A3 / docs/release/OUTPUT-LEVEL.md: C57 | Limiter is OFF by default with zero added latency; LIMIT is an opt-in switch | Change limiter default description to OFF with zero added latency (opt-in LIMIT switch). |
| `docs/MPC.md:749` | the presets are levelled on their loudest 100 milliseconds | docs/release/OUTPUT-LEVEL.md: C55 | The loudest 100 ms as levelling measure is refuted for plucks/keys because it hides sustain; presets are levelled to stock Akai instruments | Remove 100 ms loudest window levelling claim and reference stock Akai instrument levelling. |
| `docs/MPC.md:760` | Default ON: the constant kOutputLimiterDefaultOn in shell-mpc/output_stage.h is the one place to flip it. | docs/release/OUTPUT-LEVEL.md: A3 / docs/release/OUTPUT-LEVEL.md: C57 | Limiter is OFF by default with zero latency (LIMIT is an opt-in switch) | Update default to OFF (kOutputLimiterDefaultOn = false) with zero latency. |
| `docs/MPC.md:761` | A chunk without the member (a project saved before) leaves the setting as it is, that is the default (ON); a chunk that stores 0 stays off. | docs/release/OUTPUT-LEVEL.md: A3 | Limiter default is OFF (0); an unversioned or missing chunk member defaults to limiter OFF | Update chunk restore documentation so default state when omitted is OFF (0). |
| `docs/MPC.md:932` | The output limiter is inactive (env == 1.0f) during this tail; the 6 dB make-up gain brings the tail up | docs/release/OUTPUT-LEVEL.md: A2 / docs/release/OUTPUT-LEVEL.md: C53 | Make-up gain is unity (0 dB), not +6 dB or +18 dB | Correct the denormal investigation section to note unity staging (0 dB) rather than 6 dB make-up gain. |

### docs/RELEASE.md

#### `docs/RELEASE.md`

*Checked, nothing found.*

### docs/release/*.md

#### `docs/release/AUDIBLE-CHANGES-ROUND-13.md`

*Checked, nothing found.*

#### `docs/release/CHANGELOG-draft.md`

| file:line | the statement | what contradicts it (file:line or measured fact) | which statement is true and why | proposed fix (one sentence) |
|---|---|---|---|---|
| `docs/release/CHANGELOG-draft.md:41` | All 160 factory presets are calibrated using an ITU-R BS.1770 K-weighted perceptual loudness standard across category-specific playing gestures (loudest 100 ms window) | docs/release/OUTPUT-LEVEL.md: A1, C55 | Presets are levelled to stock Akai instruments under unity staging; 100 ms window is refuted | Rephrase changelog item to state presets are levelled to match stock Akai MPC instruments under unity staging. |
| `docs/release/CHANGELOG-draft.md:42` | Factory presets have been calibrated to sit at the exact same loudness as the stock Akai instruments (-1.0 dB make-up gain, look-ahead peak limiter at -0.5 dBFS)... enabled by default | docs/release/OUTPUT-LEVEL.md: A2, A3 | Default sound = unity gain staging (0 dB make-up gain), limiter OFF by default with zero latency | Correct gain to unity (0 dB) and state that the limiter is OFF by default. |
| `docs/release/CHANGELOG-draft.md:53` | The output stage applies a shared -1.0 dB make-up gain (synthyboy::kMakeupGainDb, matching stock MPC instruments levelling) with a look-ahead peak limiter (kOutputLimiterDefaultOn=true...) | docs/release/OUTPUT-LEVEL.md: A2, A3 | Make-up gain is 0 dB (unity staging) and limiter default is false (OFF) | Update changelog to state 0 dB make-up gain and kOutputLimiterDefaultOn=false. |

#### `docs/release/CPU-REVIEW-CONTENT.md`

*Checked, nothing found.*

#### `docs/release/CPU-REVIEW-FILTER-FX.md`

*Checked, nothing found.*

#### `docs/release/CPU-REVIEW-MEMORY.md`

*Checked, nothing found.*

#### `docs/release/CPU-REVIEW-OSC.md`

*Checked, nothing found.*

#### `docs/release/CPU-REVIEW-PROFILE.md`

*Checked, nothing found.*

#### `docs/release/CPU-REVIEW-VOICE-MOD.md`

*Checked, nothing found.*

#### `docs/release/DEVICE-CHECKLIST.md`

| file:line | the statement | what contradicts it (file:line or measured fact) | which statement is true and why | proposed fix (one sentence) |
|---|---|---|---|---|
| `docs/release/DEVICE-CHECKLIST.md:36` | Perform listening test on MPC XL with +18 dB make-up gain \| Look at: Audible clipping and limiting \| Pass: Lowering track by 6 dB removed audible limiting (confirmed +18 dB make-up gain is too hot, requires re-levelling) | docs/release/OUTPUT-LEVEL.md: C53 | Staged make-up was +9 dB in ov40 (not +18 dB) and final staging is unity (0 dB) | Clarify that the test confirmed clipping and led to unity gain staging (0 dB). |

#### `docs/release/DEVICE-ROUND-FINDINGS-2.md`

*Checked, nothing found.*

#### `docs/release/HISTORY-by-week.md`

*Checked, nothing found.*

#### `docs/release/LEVELLING-STOCK-2026-11-19.md`

| file:line | the statement | what contradicts it (file:line or measured fact) | which statement is true and why | proposed fix (one sentence) |
|---|---|---|---|---|
| `docs/release/LEVELLING-STOCK-2026-11-19.md:3` | the output limiter (core/include/synthyboy/output_stage.h, ceiling -0.5 dBFS) is a guard that normally idles | docs/release/OUTPUT-LEVEL.md: A3 / Section F | Limiter is OFF by default with zero latency (LIMIT is opt-in); LEVELLING-STOCK-2026-11-19.md is superseded | Add prominent header banner marking document superseded by docs/release/OUTPUT-LEVEL.md. |
| `docs/release/LEVELLING-STOCK-2026-11-19.md:30` | kMakeupGainDb = -1.0 dB: the rule's value under the round 2 objective | docs/release/OUTPUT-LEVEL.md: A2 / docs/release/OUTPUT-LEVEL.md: C59 | The make-up gain -2.5 dB / -1.0 dB is superseded by unity (0 dB staging) | Note that -1.0 dB make-up gain was superseded by unity (0 dB) in docs/release/OUTPUT-LEVEL.md. |
| `docs/release/LEVELLING-STOCK-2026-11-19.md:49` | Make-up gain of the measurement: -1.0 dB. Medians per category; peaks in dBFS after the output stage (what the host receives; the limiter's ceiling is -0.5 dBFS) | docs/release/OUTPUT-LEVEL.md: A2, A3 | Measurements reflect the intermediate limiter-on / -1.0 dB variant, superseded by unity staging with limiter off | Mark tables as historical reference for the superseded -1.0 dB / limiter-on experiment. |

#### `docs/release/OPEN-QUESTIONS.md`

*Checked, nothing found.*

#### `docs/release/SOUND-FIXES-1.md`

| file:line | the statement | what contradicts it (file:line or measured fact) | which statement is true and why | proposed fix (one sentence) |
|---|---|---|---|---|
| `docs/release/SOUND-FIXES-1.md:12` | A "limiter" érzés oka valószínűleg a host: +18 dB-es erősítés mellett a preset-ek 8-hangos akkordja simán beleütközik a host limiterébe | docs/release/OUTPUT-LEVEL.md: C53, B3 | +18 dB make-up was never in shipping ov40 (shipped default was +9 dB) and final staging is unity (0 dB) | Add pointer to docs/release/OUTPUT-LEVEL.md stating the +18 dB text was based on stale assumptions. |
| `docs/release/SOUND-FIXES-1.md:37` | The rule (deviates from the brief's suggestion "short-term max for percussive, integrated for sustained"): fast_max (loudest 100 ms of the render) | docs/release/OUTPUT-LEVEL.md: C55 | The loudest 100 ms as levelling measure is refuted (it hides sustain in decaying sounds); superseded by stock levelling | Mark Section 2 as superseded by stock reference calibration in docs/release/OUTPUT-LEVEL.md. |
| `docs/release/SOUND-FIXES-1.md:348` | Recommendation: SYNTHYBOY_MPC_OUTPUT_GAIN_DB=6. At +6 dB no preset exceeds 0 dBFS with the single-note gesture | docs/release/OUTPUT-LEVEL.md: A2 / docs/release/OUTPUT-LEVEL.md: Section F | Make-up gain is unity (0 dB); +6 dB recommendation was superseded | Mark recommendation superseded by unity gain staging (0 dB) in docs/release/OUTPUT-LEVEL.md. |
| `docs/release/SOUND-FIXES-1.md:409` | The make-up gain: the build option SYNTHYBOY_MPC_OUTPUT_GAIN_DB (default 18): recommendation 6 | docs/release/OUTPUT-LEVEL.md: A2 | Make-up gain is 0 dB (unity gain staging), not 18 dB or 6 dB | Update make-up gain note to specify 0 dB unity staging. |

#### `docs/release/SOUND-SET-BATCH-1.md`

*Checked, nothing found.*

#### `docs/release/SOUND-SET-BATCH-2.md`

*Checked, nothing found.*

#### `docs/release/SOUND-SET-BATCH-3-PLAN.md`

*Checked, nothing found.*

#### `docs/release/SOUND-SET-CATEGORIES.md`

*Checked, nothing found.*

#### `docs/release/SOUND-SET-SURVEY.md`

| file:line | the statement | what contradicts it (file:line or measured fact) | which statement is true and why | proposed fix (one sentence) |
|---|---|---|---|---|
| `docs/release/SOUND-SET-SURVEY.md:3` | Quantitative survey of the 112 factory presets in content/presets/ against the 2637 engine parameters in docs/params.json | docs/release/CHANGELOG-draft.md:36 / content/presets/ (160 presets) | The factory preset count is 160 presets across 6 categories | Update preset count and analysis to 160 presets and merge with docs/release/survey.md. |

#### `docs/release/TEMPLATE.md`

| file:line | the statement | what contradicts it (file:line or measured fact) | which statement is true and why | proposed fix (one sentence) |
|---|---|---|---|---|
| `docs/release/TEMPLATE.md:1` | # The reusable UI kit and the starter template (MpcKit) [entire file is an identical copy of docs/ui-components/TEMPLATE.md] | docs/ui-components/TEMPLATE.md (313 lines, 60874 bytes) | The document belongs in docs/ui-components/ and is redundant in docs/release/ | Delete docs/release/TEMPLATE.md or replace its content with a pointer to docs/ui-components/TEMPLATE.md. |

#### `docs/release/TIER-LEVEL-COMPENSATION.md`

*Checked, nothing found.*

#### `docs/release/WAVETABLE-PACK.md`

*Checked, nothing found.*

#### `docs/release/preset-todo.md`

| file:line | the statement | what contradicts it (file:line or measured fact) | which statement is true and why | proposed fix (one sentence) |
|---|---|---|---|---|
| `docs/release/preset-todo.md:7` | A *-os presetek a második adag. | docs/release/OUTPUT-LEVEL.md: A6 / docs/release/DEVICE-ROUND-FINDINGS-2.md:19 | No " *" in any program name the host sees; asterisk marker was removed | Remove asterisk convention note from preset review instructions. |

#### `docs/release/survey.md`

| file:line | the statement | what contradicts it (file:line or measured fact) | which statement is true and why | proposed fix (one sentence) |
|---|---|---|---|---|
| `docs/release/survey.md:6` | Total Presets Analyzed: 112 | docs/release/CHANGELOG-draft.md:36 / content/presets/ (160 presets) | The factory preset library contains 160 presets, not 112 | Update survey to cover all 160 factory presets (or merge with docs/release/SOUND-SET-SURVEY.md). |

### docs/ui-components/*

#### `docs/ui-components/LOGGING.md`

*Checked, nothing found.*

#### `docs/ui-components/PAGES.md`

*Checked, nothing found.*

#### `docs/ui-components/README.md`

*Checked, nothing found.*

#### `docs/ui-components/REUSE.md`

*Checked, nothing found.*

#### `docs/ui-components/TEMPLATE.md`

*Checked, nothing found.*

#### `docs/ui-components/bars.md`

*Checked, nothing found.*

#### `docs/ui-components/buttons.md`

*Checked, nothing found.*

#### `docs/ui-components/carousel.md`

*Checked, nothing found.*

#### `docs/ui-components/composites.md`

*Checked, nothing found.*

#### `docs/ui-components/core.md`

*Checked, nothing found.*

#### `docs/ui-components/display.md`

*Checked, nothing found.*

#### `docs/ui-components/env-graph.md`

*Checked, nothing found.*

#### `docs/ui-components/env-screens.md`

*Checked, nothing found.*

#### `docs/ui-components/filter-display.md`

*Checked, nothing found.*

#### `docs/ui-components/filter-screens.md`

*Checked, nothing found.*

#### `docs/ui-components/fx-chain.md`

*Checked, nothing found.*

#### `docs/ui-components/fx-display.md`

*Checked, nothing found.*

#### `docs/ui-components/fx-editor.md`

*Checked, nothing found.*

#### `docs/ui-components/fx-gallery.md`

*Checked, nothing found.*

#### `docs/ui-components/helpers.md`

*Checked, nothing found.*

#### `docs/ui-components/knob-panel.md`

*Checked, nothing found.*

#### `docs/ui-components/knob.md`

*Checked, nothing found.*

#### `docs/ui-components/led-list.md`

*Checked, nothing found.*

#### `docs/ui-components/lfo-parts.md`

*Checked, nothing found.*

#### `docs/ui-components/lfo-screens.md`

*Checked, nothing found.*

#### `docs/ui-components/list-popup.md`

*Checked, nothing found.*

#### `docs/ui-components/load-bar.md`

*Checked, nothing found.*

#### `docs/ui-components/macros.md`

*Checked, nothing found.*

#### `docs/ui-components/master-screen.md`

*Checked, nothing found.*

#### `docs/ui-components/mixer.md`

*Checked, nothing found.*

#### `docs/ui-components/mod-screens.md`

*Checked, nothing found.*

#### `docs/ui-components/mod-source-board.md`

*Checked, nothing found.*

#### `docs/ui-components/modulations-page.md`

*Checked, nothing found.*

#### `docs/ui-components/note-mask.md`

*Checked, nothing found.*

#### `docs/ui-components/number-field.md`

*Checked, nothing found.*

#### `docs/ui-components/osc-screens.md`

*Checked, nothing found.*

#### `docs/ui-components/pictogram-selector.md`

*Checked, nothing found.*

#### `docs/ui-components/qgroup.md`

*Checked, nothing found.*

#### `docs/ui-components/shape-editor.md`

*Checked, nothing found.*

#### `docs/ui-components/stepper.md`

*Checked, nothing found.*

#### `docs/ui-components/text-entry.md`

*Checked, nothing found.*

#### `docs/ui-components/theme.md`

*Checked, nothing found.*

#### `docs/ui-components/toggle.md`

*Checked, nothing found.*

#### `docs/ui-components/type-bar.md`

*Checked, nothing found.*

#### `docs/ui-components/voice-displays.md`

*Checked, nothing found.*

#### `docs/ui-components/voice-screen.md`

*Checked, nothing found.*

### .agents/skills/**

#### `.agents/skills/wavetable-lab/SKILL.md`

*Checked, nothing found.*

### .hw-local/mpc-ui-plans/*.md

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/6-szuro-gorbe.md`

| file:line | the statement | what contradicts it (file:line or measured fact) | which statement is true and why | proposed fix (one sentence) |
|---|---|---|---|---|
| `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/6-szuro-gorbe.md:12` | a +18 dB-es kimenet miatt a sávmérő amúgy is kiakad | docs/release/OUTPUT-LEVEL.md: A2, C53 | Make-up gain is unity (0 dB), not +18 dB, so meters stay below 0 dBFS under proper calibration | Remove premise that +18 dB make-up clips the meters. |

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/ESTE-2026-11-18.md`

| file:line | the statement | what contradicts it (file:line or measured fact) | which statement is true and why | proposed fix (one sentence) |
|---|---|---|---|---|
| `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/ESTE-2026-11-18.md:11` | Csúcskorlát a szintezésnél (Nylon Zone Pluck miatt). Javaslat: legyen; a +18 dB-es kimeneti fokozat mellett a raw csúcs max 0 dBFS | docs/release/OUTPUT-LEVEL.md: A2, C53 | Shell make-up gain is 0 dB (unity staging), not +18 dB | Update plan entry to reflect unity gain staging (0 dB). |

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/HANDOFF-2026-11-18-coordinator.md`

| file:line | the statement | what contradicts it (file:line or measured fact) | which statement is true and why | proposed fix (one sentence) |
|---|---|---|---|---|
| `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/HANDOFF-2026-11-18-coordinator.md:36` | Contains everything up to: 160 presets re-levelled (LUFS-based) | docs/release/OUTPUT-LEVEL.md: A1, C55 | Presets are levelled against stock Akai MPC instruments under unity gain staging, not LUFS | Clarify that levelling is calibrated against stock Akai instruments rather than ITU-R BS.1770 LUFS. |
| `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/HANDOFF-2026-11-18-coordinator.md:69` | version changes from 1.0.0.0 | docs/release/OUTPUT-LEVEL.md / memory/synthyboy-release-version.md:11 | Every release version is 0.1.0 build 1 | Ensure all version references are updated from 1.0.0.0 to 0.1.0 build 1. |

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/REGGEL-szurogorbe.md`

*Checked, nothing found.*

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/STATE-2026-11-16-limit.md`

| file:line | the statement | what contradicts it (file:line or measured fact) | which statement is true and why | proposed fix (one sentence) |
|---|---|---|---|---|
| `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/STATE-2026-11-16-limit.md:12` | Owner questions open: ... fixed +18 dB output or adjustable. | docs/release/OUTPUT-LEVEL.md: A2, C53 | Output level is resolved: unity gain staging (0 dB), levels live in preset master.volume | Mark question as closed by the owner's 2026-11-19 unity gain staging decision. |

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/STATE-2026-11-17-pause.md`

| file:line | the statement | what contradicts it (file:line or measured fact) | which statement is true and why | proposed fix (one sentence) |
|---|---|---|---|---|
| `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/STATE-2026-11-17-pause.md:31` | Output level policy: with +18 dB make-up and no limiter a full 8-note chord peaks +6..+17 dBFS on the host track meter | docs/release/OUTPUT-LEVEL.md: A2, C53 | Make-up gain is unity (0 dB) with limiter off by default, and presets are calibrated to stay under 0 dBFS | Update handoff note to reference unity gain staging (0 dB) in docs/release/OUTPUT-LEVEL.md. |

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/TERV-2026-11-14.md`

| file:line | the statement | what contradicts it (file:line or measured fact) | which statement is true and why | proposed fix (one sentence) |
|---|---|---|---|---|
| `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/TERV-2026-11-14.md:24` | Tény: 71b84ba-ban fix +6 dB make-up + limiter (plafon −1 dBFS, azonnali felfutás, 120 ms visszatérés) | docs/release/OUTPUT-LEVEL.md: A2, A3, C58 | Make-up gain is 0 dB (unity), limiter ceiling is -0.5 dBFS (when on), limiter is OFF by default | Mark historical plan notes as superseded by docs/release/OUTPUT-LEVEL.md. |
| `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/TERV-2026-11-14.md:197` | A2 DÖNTÉS (tervező; az owner felülbírálhatja): a make-up erősítés NEM nő (marad +6 dB, limiter −1 dB-es plafonnal) | docs/release/OUTPUT-LEVEL.md: A2, A3 | Owner decided unity gain staging (0 dB) and limiter off by default | Annotate with owner's 2026-11-19 decision reversing +6 dB and setting unity staging. |
| `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/TERV-2026-11-14.md:217` | H4 csomag a koordinátornak — kimeneti fokozat: +18 dB alapérték, LIMITER NÉLKÜL | docs/release/OUTPUT-LEVEL.md: A2, C53 | +18 dB make-up gain caused severe clipping (+10.7 dBFS chord peaks) and was replaced by 0 dB unity gain staging | Mark +18 dB proposal as superseded by unity gain staging in docs/release/OUTPUT-LEVEL.md. |

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/coverage-gaps.md`

*Checked, nothing found.*

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/gemini-optimisation-scoreboard.md`

*Checked, nothing found.*

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/gepi-kapuk-eredmeny.md`

| file:line | the statement | what contradicts it (file:line or measured fact) | which statement is true and why | proposed fix (one sentence) |
|---|---|---|---|---|
| `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/gepi-kapuk-eredmeny.md:154` | Új build (71b84ba... skála −60..−12 dBFS, +6 dB make-up gain) | docs/release/OUTPUT-LEVEL.md: A2 | +6 dB make-up gain was superseded by unity gain staging (0 dB) | Mark historical test result as reflecting earlier +6 dB experiment. |
| `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/gepi-kapuk-eredmeny.md:505` | Következtetés: ezen a mérőn a SynthyBoy NEM halkabb a stock hangszereknél (vel 127-nél a -1 dBFS-es plafonon ül) | docs/release/OUTPUT-LEVEL.md: C58 | Stage ceiling is -0.5 dBFS (not -1.0 dBFS) when limiter is active | Note that stage ceiling is -0.5 dBFS and limiter is now off by default. |
| `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/gepi-kapuk-eredmeny.md:547` | kimeneti fokozat: +18 dB fix make-up gain (SYNTHYBOY_MPC_OUTPUT_GAIN_DB=18), LIMITER NÉLKÜL | docs/release/OUTPUT-LEVEL.md: A2, C53 | +18 dB gain caused +10.7 dBFS chord clipping and is replaced by 0 dB unity staging | Mark historical test package configuration as superseded by unity gain staging. |

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/hangkeszlet-1-meghallgatasi-lap.md`

*Checked, nothing found.*

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/hangkeszlet-3-meghallgatasi-lap.md`

*Checked, nothing found.*

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/inhost-draw-prototype.md`

*Checked, nothing found.*

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/kerdesek-tervezonek.md`

*Checked, nothing found.*

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/kozosseg-es-megosztas.md`

*Checked, nothing found.*

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/layout-lint-report.md`

*Checked, nothing found.*

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/macro-names-proposed.md`

*Checked, nothing found.*

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/pad-pressure-presets-proposed.md`

*Checked, nothing found.*

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/pre-release-format.md`

*Checked, nothing found.*

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/research-draw-into-host.md`

*Checked, nothing found.*

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/review-3.md`

*Checked, nothing found.*

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/review-4.md`

*Checked, nothing found.*

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/review-5.md`

*Checked, nothing found.*

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/review-6-notes.md`

*Checked, nothing found.*

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/review-new-elements-1.md`

*Checked, nothing found.*

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/review-new-elements-2.md`

*Checked, nothing found.*

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/review-osc-ui-1.md`

*Checked, nothing found.*

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/review-osc-ui-2.md`

*Checked, nothing found.*

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/review-queue.md`

*Checked, nothing found.*

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/robustness-review.md`

| file:line | the statement | what contradicts it (file:line or measured fact) | which statement is true and why | proposed fix (one sentence) |
|---|---|---|---|---|
| `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/robustness-review.md:80` | F9. Output level: +18 dB make-up gain, limiter off, no ceiling. output_stage.h (kLimiterEnabled=false, kMakeupGainDb=18.0f) | docs/release/OUTPUT-LEVEL.md: A2, C53 | Make-up gain is unity (0 dB), not +18 dB; +18 dB caused massive host clipping | Update finding F9 to reflect that make-up gain is 0 dB (unity gain staging). |

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/stock-animacio-gepi.md`

*Checked, nothing found.*

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/ui-elem-envelope-lfo.md`

*Checked, nothing found.*

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/ui-elem-filter.md`

*Checked, nothing found.*

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/ui-elem-mastervoice.md`

*Checked, nothing found.*

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/ui-elem-mixfx.md`

*Checked, nothing found.*

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/ui-elem-modmatrix.md`

*Checked, nothing found.*

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/ui-elem-protokoll.md`

*Checked, nothing found.*

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/xl-teszt-lista.md`

*Checked, nothing found.*

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/xl-tier-analysis.md`

*Checked, nothing found.*

#### `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/xl-tier-bench.md`

*Checked, nothing found.*

### Memory files (~/.claude/.../memory/*.md)

#### `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/MEMORY.md`

*Checked, nothing found.*

#### `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/agy-worker-model-flash.md`

*Checked, nothing found.*

#### `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/akai-manual-first.md`

*Checked, nothing found.*

#### `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/cleanup-own-worker-leftovers.md`

*Checked, nothing found.*

#### `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/copilot-studio-agents-hungarian.md`

*Checked, nothing found.*

#### `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/feedback-autonomous-hw-debugging.md`

*Checked, nothing found.*

#### `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/feedback-build-slots-after-panic.md`

*Checked, nothing found.*

#### `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/feedback-cost-vs-speed.md`

*Checked, nothing found.*

#### `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/feedback-find-another-way.md`

*Checked, nothing found.*

#### `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/feedback-md-tasks-done-then-delete.md`

*Checked, nothing found.*

#### `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/feedback-never-stop-running-work.md`

*Checked, nothing found.*

#### `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/feedback-worker-ladder.md`

*Checked, nothing found.*

#### `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/kick-mobile-fullscreen-only.md`

*Checked, nothing found.*

#### `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/kick-mpc-local-dev.md`

*Checked, nothing found.*

#### `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/local-browser-worker-skill.md`

*Checked, nothing found.*

#### `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/local-models-disk.md`

*Checked, nothing found.*

#### `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/local-worker-skill.md`

*Checked, nothing found.*

#### `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/mobilkresz-situation-text-truth.md`

*Checked, nothing found.*

#### `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/mpc-hardware-exclusive.md`

*Checked, nothing found.*

#### `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/mpc-key61-3101-setup.md`

| file:line | the statement | what contradicts it (file:line or measured fact) | which statement is true and why | proposed fix (one sentence) |
|---|---|---|---|---|
| `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/mpc-key61-3101-setup.md:30` | The packaged plugin (30 skin tabs, 112 presets) is installed and loads. | docs/release/CHANGELOG-draft.md:6, 36 / content/presets/ (160 presets) | The plugin now contains 160 factory presets across 34/35 tabs | Update preset count from 112 to 160 factory presets. |

#### `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/mpc-own-test-project.md`

*Checked, nothing found.*

#### `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/mpc-xl-device.md`

*Checked, nothing found.*

#### `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/proper-develop-moving.md`

*Checked, nothing found.*

#### `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/proper-divergence-precedence.md`

*Checked, nothing found.*

#### `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/proper-history-reset.md`

*Checked, nothing found.*

#### `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/proper-m08-vst3-host.md`

*Checked, nothing found.*

#### `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/synthyboy-community-release-session.md`

*Checked, nothing found.*

#### `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/synthyboy-key61-qlink-flicker-known-bug.md`

*Checked, nothing found.*

#### `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/synthyboy-max-quality.md`

*Checked, nothing found.*

#### `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/synthyboy-mpc-planner-only.md`

*Checked, nothing found.*

#### `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/synthyboy-presets-not-release-blocker.md`

*Checked, nothing found.*

#### `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/synthyboy-release-scope.md`

*Checked, nothing found.*

#### `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/synthyboy-release-version.md`

*Checked, nothing found.*

#### `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/synthyboy-unity-gain-no-limiter.md`

*Checked, nothing found.*

#### `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/synthyboy-xl-first.md`

*Checked, nothing found.*

#### `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/xcode27-devicehub.md`

*Checked, nothing found.*

### Owner configuration files (Needs the owner)

#### `/Users/szabi/.claude/CLAUDE.md`

| file:line | the statement | what contradicts it (file:line or measured fact) | which statement is true and why | proposed fix (one sentence) |
|---|---|---|---|---|
| `/Users/szabi/.claude/CLAUDE.md:8` | Tervezés/architektúra/task-bontás: erős modell (Fable/Opus), a fő session rétege. | memory/feedback-worker-ladder.md:14 | Owner decided: "never Fable or Opus 4.x"; use Gemini flash first, then Opus 5.5 / Sonnet 5.5 | Remove Fable from planning models and specify Opus 5.5. |
| `/Users/szabi/.claude/CLAUDE.md:27` | Kapuk (build/teszt) — csak ha a változás indokolja... Kódváltozás: inkrementális build + érintett suite-ok, TODO-listánként EGYSZER a végén (nem pontonként). | Safian/AGENTS.md:4 / memory/feedback-build-slots-after-panic.md | Post-panic rules require heavy builds to pass through buildslot.sh with -j4 and at most 2 concurrent build slots | Add buildslot.sh wrapper rule and -j4 limit to global CLAUDE.md build section. |

#### `/Users/szabi/Documents/Projects/Safian/CLAUDE.md`

*Checked, nothing found.*

#### `/Users/szabi/Documents/Projects/CLAUDE.md`

*Checked, nothing found.*

---

## 3. Numbers Appearing in Multiple Places with Divergent Values

This section identifies quantitative metrics that appear across the documentation with conflicting values, and identifies the authoritative document that must own each number.

### 3.1 Shell Make-Up Gain and Output Staging
| Stated Value | Occurrences in Project | Authoritative Value & Owner | Rationale |
|---|---|---|---|
| **+18 dB** | `docs/release/SOUND-FIXES-1.md:12,68,339,409`, `docs/release/DEVICE-CHECKLIST.md:36`, `.hw-local/mpc-ui-plans/TERV-2026-11-14.md:217`, `robustness-review.md:80`, `STATE-2026-11-17-pause.md:31` | **0.0 dB (Unity Staging)**<br>*Owner*: `docs/release/OUTPUT-LEVEL.md: A2` | +18 dB was an experimental proposal that pushed 4-note chord peaks to +10.7 dBFS, causing severe clipping at 0 dBFS host limiters. Unity gain staging is the decided owner rule. |
| **+9 dB** | `docs/release/OUTPUT-LEVEL.md: B3` (ov40 package default) | **0.0 dB (Unity Staging)**<br>*Owner*: `docs/release/OUTPUT-LEVEL.md: A2` | +9 dB was the compiled source default in ov40 builds. It caused 156 of 160 presets to clip above 0 dBFS and was superseded by unity staging. |
| **+6 dB** | `docs/release/SOUND-FIXES-1.md:348,371`, `docs/MPC.md:932`, `TERV-2026-11-14.md:24,194,197`, `gepi-kapuk-eredmeny.md:154`, `HANDOFF-2026-11-18-coordinator.md:39` | **0.0 dB (Unity Staging)**<br>*Owner*: `docs/release/OUTPUT-LEVEL.md: A2` | +6 dB was recommended during early perceptual testing. It was superseded by the owner's 2026-11-19 decision for unity gain staging. |
| **-2.5 dB / -1.0 dB** | `docs/MPC.md:746`, `docs/release/CHANGELOG-draft.md:42,53`, `docs/release/LEVELLING-STOCK-2026-11-19.md:30,40,49` | **0.0 dB (Unity Staging)**<br>*Owner*: `docs/release/OUTPUT-LEVEL.md: A2, C59` | -2.5 dB and -1.0 dB were calibration gains for the intermediate limiter-on variant. Superseded by unity staging with limiter OFF. |

### 3.2 Output Limiter Default State and Ceiling
| Stated Value | Occurrences in Project | Authoritative Value & Owner | Rationale |
|---|---|---|---|
| **Default ON (3 ms latency)** | `docs/MPC.md:747,760,761`, `docs/release/CHANGELOG-draft.md:42,53`, `docs/release/LEVELLING-STOCK-2026-11-19.md:3` | **Default OFF (0 ms latency)**<br>*Owner*: `docs/release/OUTPUT-LEVEL.md: A3` | The owner explicitly reversed the limiter default on 2026-11-19: LIMIT is an opt-in switch with zero latency. |
| **Ceiling -1.0 dBFS** | `TERV-2026-11-14.md:24,195`, `gepi-kapuk-eredmeny.md:505`, `docs/release/OUTPUT-LEVEL.md: C58` | **Ceiling -0.5 dBFS**<br>*Owner*: `docs/release/OUTPUT-LEVEL.md: B5, C58`, `core/include/synthyboy/output_stage.h` | The old brickwall limiter used -1.0 dBFS. The shared `OutputStage` limiter uses `kCeiling = 0.94406088f` (-0.5 dBFS) to provide DAC inter-sample peak headroom. |

### 3.3 Factory Preset Levelling Targets
| Stated Target | Occurrences in Project | Authoritative Target & Owner | Rationale |
|---|---|---|---|
| **-23 LUFS / 100 ms fast_max** | `docs/release/SOUND-FIXES-1.md:37,55,121`, `HANDOFF-2026-11-18-coordinator.md:36` | **Stock Akai Device Medians**<br>*Owner*: `docs/release/OUTPUT-LEVEL.md: B1`, `scripts/stock-level-targets.json` | BS.1770 loudness over a 100 ms window hid sustain and caused plucks/keys to sit 20 dB below stock instruments. Stock reference peaks/sustain medians govern levelling. |
| **-12.5 LUFS integrated** | `docs/release/OUTPUT-LEVEL.md: C52` (refuted claim) | **Stock Akai Device Medians**<br>*Owner*: `docs/release/OUTPUT-LEVEL.md: B1` | ov38 was claimed quiet due to -12.5 LUFS being conservative, but measurements showed presets were clipping at +10 dBFS. |

### 3.4 CPU Budgets and Per-Preset Resource Usage
| Metric | Divergent Stated Values | Authoritative Owner | Rationale |
|---|---|---|---|
| **Key 61 CPU Budget** | 70% of 1 core (2.03 ms of 2.902 ms block) in `docs/MPC.md:350`, `CPU-REVIEW-PROFILE.md:296` vs 100% (2.902 ms deadline) in `docs/release/CHANGELOG-draft.md:90` | `docs/MPC.md` 'Device CPU' / `docs/release/CPU-REVIEW-PROFILE.md` | 70% budget provides safety headroom for host UI and background tasks on Cortex-A17. |
| **MPC XL CPU Budget** | 50% CPU budget in `docs/release/DEVICE-CHECKLIST.md:23`, `xl-tier-bench.md:37,87` | `docs/release/DEVICE-CHECKLIST.md:23` | XL targets 50% budget on Cortex-A76 core 6 at 16 voices. |
| **Stretched Glass Choir CPU** | 23.5% (morph build 6.2%, wt read 5.9%) in `CPU-REVIEW-OSC.md:45` vs 56.78% median in `CPU-REVIEW-PROFILE.md:301` | `.hw-local/mpc-perf/round12/k61-breakdown/` CSV logs | 23.5% measured oscillator sub-block isolation; 56.78% represents full-voice render on Key 61 tier low. |
| **Lush Analog Pad CPU (XL)** | 35.4% p99 in `DEVICE-CHECKLIST.md:23` vs 31.5% p99 in `xl-tier-analysis.md:5` | `/tmp/sbtb/xl-r4-all.csv` / `xl-tier-bench.md` Round 4 | Round 4 bench established 35.4% p99 under profile `high,voices=16,control=economy`. |

### 3.5 Start-Up Times
| Phase | Divergent Stated Values | Authoritative Owner | Rationale |
|---|---|---|---|
| **Time to First Audio (XL)** | 657 ms in `DEVICE-CHECKLIST.md:25`, `STATE-2026-11-17-pause.md` vs 2401 ms (2.4 s host overhead) in `docs/MPC.md:1396` | `docs/MPC.md` 'Start-up time' and `docs/release/DEVICE-CHECKLIST.md` | 657 ms measures plugin open to first audio; 2401 ms includes full host cold-load and project instantiation. |
| **Binary dlopen + VSTPluginMain** | 36 ms in `docs/MPC.md:325` | `docs/MPC.md:325` | Measured hardware dlopen time for stripped release binary. |
| **GPU Helper Start** | 133 ms in `DEVICE-CHECKLIST.md:25` vs ~150 ms in `docs/MPC.md:329` | `docs/release/DEVICE-CHECKLIST.md:25` | Measured daemon launch time over UNIX seqpacket socket. |

### 3.6 Release Version and Build Number
| Stated Version | Occurrences in Project | Authoritative Owner | Rationale |
|---|---|---|---|
| **1.0.0 / 1.0.0.0** | `HANDOFF-2026-11-18-coordinator.md:69`, `synthyboy-release-version.md:13`, VST host journal | **0.1.0 build 1**<br>*Owner*: `synthyboy-release-version.md:11`, `OUTPUT-LEVEL.md` | 1.0.0.0 was the default VST SDK dispatch version. The owner explicitly decided on 2026-11-18 that the first public release is version 0.1.0 build 1 across all artifacts. |
| **0.0.1** | Early manifest templates | **0.1.0 build 1** | Superseded by owner decision. |

---

## 4. Duplicate Documents to Merge or Mark Superseded

1. **`docs/release/TEMPLATE.md` vs `docs/ui-components/TEMPLATE.md`**:
   - **Issue**: Identical 100% byte-for-byte duplicate (60,874 bytes, 313 lines). Both documents contain 'The reusable UI kit and the starter template (MpcKit)'.
   - **Disposition**: `docs/release/TEMPLATE.md` is misplaced in `docs/release/`. It should be deleted or replaced with a 1-line pointer redirecting to `docs/ui-components/TEMPLATE.md`.

2. **`docs/release/survey.md` vs `docs/release/SOUND-SET-SURVEY.md`**:
   - **Issue**: Both documents present a 'Factory Sound Set Survey' evaluating 112 factory presets against 2637 engine parameters. `survey.md` (428 lines) adds preset clustering, while `SOUND-SET-SURVEY.md` (349 lines) adds gap analysis. Both are stale because the library now contains 160 factory presets.
   - **Disposition**: Merge the unique analyses into a single updated `docs/release/SOUND-SET-SURVEY.md` analyzing all 160 presets, and delete `survey.md`.

3. **`docs/release/LEVELLING-STOCK-2026-11-19.md`**:
   - **Issue**: Formally superseded by `docs/release/OUTPUT-LEVEL.md` (Section F). Documents the intermediate round where make-up gain was -1.0 dB and the limiter was active by default.
   - **Disposition**: Retain for historical reference, but prepend an explicit deprecation banner referencing `docs/release/OUTPUT-LEVEL.md` as the single source of truth.

4. **`docs/release/SOUND-FIXES-1.md` (Level Sections)**:
   - **Issue**: Level calibration sections (Sections 2, 3, 7, 8) rely on +18 dB / +6 dB make-up gain proposals and BS.1770 100 ms loudness matching.
   - **Disposition**: Formally superseded by `docs/release/OUTPUT-LEVEL.md` (Section F). Add a banner indicating level recommendations are superseded by unity gain staging.

5. **`docs/MPC.md` (Section 'Output level', lines 742-765)**:
   - **Issue**: Documents `kMakeupGainDb = -1.0 dB` and limiter default ON.
   - **Disposition**: Formally superseded by `docs/release/OUTPUT-LEVEL.md` (Section F). Replace the text with a pointer to `OUTPUT-LEVEL.md`.

6. **`.hw-local/mpc-perf/level-analysis/REPORT.md` and `TABLE_A.md`**:
   - **Issue**: Raw analysis documents from the ov40 package measurement.
   - **Disposition**: Formally superseded by `docs/release/OUTPUT-LEVEL.md` (Section F). Retain only as historical evidence.

---

## 5. Rules Stated Differently Across Files

1. **Limiter Default State**:
   - *State A*: Limiter is ON by default (`kOutputLimiterDefaultOn = true`), inserting 3 ms lookahead latency (`docs/MPC.md:747`, `docs/release/CHANGELOG-draft.md:42,53`, `docs/release/LEVELLING-STOCK-2026-11-19.md:3`).
   - *State B*: Limiter is OFF by default with zero latency (`OUTPUT-LEVEL.md: A3`, `memory/synthyboy-unity-gain-no-limiter.md:8`).
   - *Resolution*: State B is the authoritative rule.

2. **Shell Output Gain**:
   - *State A*: Shell applies make-up gain: +18 dB (`SOUND-FIXES-1.md:12`), +6 dB (`TERV-2026-11-14.md:197`), or -1.0 dB (`docs/MPC.md:746`).
   - *State B*: Shell applies unity gain (0 dB), levels live in preset `master.volume` (`OUTPUT-LEVEL.md: A2`).
   - *Resolution*: State B is the authoritative rule.

3. **Program Name Asterisk Mark**:
   - *State A*: Presets append `" *"` when modified (`docs/MPC.md:107`, `docs/release/preset-todo.md:7`).
   - *State B*: The asterisk mark is removed by design (`work/no-asterisk`, `DEVICE-ROUND-FINDINGS-2.md:19`, `OUTPUT-LEVEL.md: A6`).
   - *Resolution*: State B is the authoritative rule.

4. **MPC XL Quality Profile**:
   - *State A*: XL was assumed or reported as profile `low` in early test logs (`OUTPUT-LEVEL.md: C49`).
   - *State B*: XL runs profile `high` (`high,voices=16,control=economy`) (`OUTPUT-LEVEL.md: B4, C49`, `docs/MPC.md:495`, `CHANGELOG-draft.md:15`).
   - *Resolution*: State B is the authoritative rule.

5. **Build Discipline & Worker Concurrency**:
   - *State A*: General rule in `~/.claude/CLAUDE.md:27` states: 'Kapuk: TODO-listánként egyszer a végén (nem pontonként).' No machine-wide lock or process limits are specified.
   - *State B*: Post-panic rule in `Safian/AGENTS.md:4` and `memory/feedback-build-slots-after-panic.md` requires: all heavy builds through `bash .hw-local/tools/buildslot.sh <cmd>` (max 2 slots, `-j4`), max 4 compiling workers, workers never run full suites.
   - *Resolution*: State B is mandatory across all Safian projects.

6. **Model Delegation Ladder**:
   - *State A*: `~/.claude/CLAUDE.md:8` permits 'Fable/Opus' for planning/architecture.
   - *State B*: `memory/feedback-worker-ladder.md` strictly orders: Gemini 3.8 Flash first; then Claude Opus 5.5 / Sonnet 5.5; strictly forbids Fable and Opus 4.x.
   - *Resolution*: State B governs worker model selection.

---

## 6. Needs the Owner

The following files reside outside the repository or represent global configuration files that agents must never edit directly. The proposed modifications are listed below for owner review and application:

### 1. `~/.claude/CLAUDE.md`
- **Finding 1 (Line 8)**: Text lists `Fable/Opus` as valid planning models (`Tervezés/architektúra/task-bontás: erős modell (Fable/Opus)`).
  - *Conflict*: On 2026-11-18, the owner established the worker ladder (`memory/feedback-worker-ladder.md`): `Fable` is strictly retired and forbidden; `Opus` must be 5.5.
  - *Proposed Edit*: Change line 8 to: `Tervezés/architektúra/task-bontás: erős modell (Opus 5.5), a fő session rétege; Fable és Opus 4.x használata tilos.`
- **Finding 2 (Line 27)**: General build gate text does not incorporate the post-kernel-panic (2026-11-16) build discipline rules.
  - *Conflict*: `Safian/AGENTS.md` and `memory/feedback-build-slots-after-panic.md` require all heavy builds to go through `buildslot.sh` with `-j4` and max 2 concurrent slots.
  - *Proposed Edit*: Add a cross-reference or excerpt of Safian Build- és tesztfegyelem Section 4 to `~/.claude/CLAUDE.md` under `Kapuk`.
- **Finding 3 (Line 34)**: Update `agy-worker` delegation guideline to specify `gemini-3.8-flash-high` as the primary agy model.

### 2. `/Users/szabi/Documents/Projects/Safian/CLAUDE.md`
- **Finding**: Symlink pointing to `/Users/szabi/Documents/Projects/Safian/AGENTS.md`.
- *Status*: Contains the complete and authoritative Section 4 'Build- és tesztfegyelem (owner, 2026-11-16)'. No contradictions found; retain as the master template.

### 3. `/Users/szabi/Documents/Projects/CLAUDE.md`
- **Finding**: Root router file (10 lines).
- *Status*: Clean router pointing to mobile web checklist. No contradictions found; no changes needed.

---

## 7. Audit Summary and Counts Per File

| Category | Total Files Audited | Files with Findings | Total Findings | Status |
|---|:---:|:---:|:---:|:---:|
| **CLAUDE.md / AGENTS.md in repo** | 3 | 0 | 0 | All clean |
| **docs/MPC.md** | 1 | 1 | 7 | 1 files need fixes |
| **docs/RELEASE.md** | 1 | 0 | 0 | All clean |
| **docs/release/*.md** | 24 | 8 | 15 | 8 files need fixes |
| **docs/ui-components/*** | 46 | 0 | 0 | All clean |
| **.agents/skills/**** | 1 | 0 | 0 | All clean |
| **.hw-local/mpc-ui-plans/*.md** | 40 | 8 | 13 | 8 files need fixes |
| **Memory files (~/.claude/.../memory/*.md)** | 36 | 1 | 1 | 1 files need fixes |
| **Owner configuration files (Needs the owner)** | 3 | 1 | 2 | 1 files need fixes |
| **TOTAL** | **155** | **19** | **38** | **19 files need attention** |

### Complete File Inventory and Finding Counts

| # | Audited File | Category | Findings Count | Status |
|---|---|---|:---:|:---:|
| 1 | `AGENTS.md` | CLAUDE.md / AGENTS.md in repo | 0 | checked, nothing found |
| 2 | `shell-mpc/ui/AGENTS.md` | CLAUDE.md / AGENTS.md in repo | 0 | checked, nothing found |
| 3 | `template-mpc/AGENTS.md` | CLAUDE.md / AGENTS.md in repo | 0 | checked, nothing found |
| 4 | `docs/MPC.md` | docs/MPC.md | 7 | **7 finding(s)** |
| 5 | `docs/RELEASE.md` | docs/RELEASE.md | 0 | checked, nothing found |
| 6 | `docs/release/AUDIBLE-CHANGES-ROUND-13.md` | docs/release/*.md | 0 | checked, nothing found |
| 7 | `docs/release/CHANGELOG-draft.md` | docs/release/*.md | 3 | **3 finding(s)** |
| 8 | `docs/release/CPU-REVIEW-CONTENT.md` | docs/release/*.md | 0 | checked, nothing found |
| 9 | `docs/release/CPU-REVIEW-FILTER-FX.md` | docs/release/*.md | 0 | checked, nothing found |
| 10 | `docs/release/CPU-REVIEW-MEMORY.md` | docs/release/*.md | 0 | checked, nothing found |
| 11 | `docs/release/CPU-REVIEW-OSC.md` | docs/release/*.md | 0 | checked, nothing found |
| 12 | `docs/release/CPU-REVIEW-PROFILE.md` | docs/release/*.md | 0 | checked, nothing found |
| 13 | `docs/release/CPU-REVIEW-VOICE-MOD.md` | docs/release/*.md | 0 | checked, nothing found |
| 14 | `docs/release/DEVICE-CHECKLIST.md` | docs/release/*.md | 1 | **1 finding(s)** |
| 15 | `docs/release/DEVICE-ROUND-FINDINGS-2.md` | docs/release/*.md | 0 | checked, nothing found |
| 16 | `docs/release/HISTORY-by-week.md` | docs/release/*.md | 0 | checked, nothing found |
| 17 | `docs/release/LEVELLING-STOCK-2026-11-19.md` | docs/release/*.md | 3 | **3 finding(s)** |
| 18 | `docs/release/OPEN-QUESTIONS.md` | docs/release/*.md | 0 | checked, nothing found |
| 19 | `docs/release/SOUND-FIXES-1.md` | docs/release/*.md | 4 | **4 finding(s)** |
| 20 | `docs/release/SOUND-SET-BATCH-1.md` | docs/release/*.md | 0 | checked, nothing found |
| 21 | `docs/release/SOUND-SET-BATCH-2.md` | docs/release/*.md | 0 | checked, nothing found |
| 22 | `docs/release/SOUND-SET-BATCH-3-PLAN.md` | docs/release/*.md | 0 | checked, nothing found |
| 23 | `docs/release/SOUND-SET-CATEGORIES.md` | docs/release/*.md | 0 | checked, nothing found |
| 24 | `docs/release/SOUND-SET-SURVEY.md` | docs/release/*.md | 1 | **1 finding(s)** |
| 25 | `docs/release/TEMPLATE.md` | docs/release/*.md | 1 | **1 finding(s)** |
| 26 | `docs/release/TIER-LEVEL-COMPENSATION.md` | docs/release/*.md | 0 | checked, nothing found |
| 27 | `docs/release/WAVETABLE-PACK.md` | docs/release/*.md | 0 | checked, nothing found |
| 28 | `docs/release/preset-todo.md` | docs/release/*.md | 1 | **1 finding(s)** |
| 29 | `docs/release/survey.md` | docs/release/*.md | 1 | **1 finding(s)** |
| 30 | `docs/ui-components/LOGGING.md` | docs/ui-components/* | 0 | checked, nothing found |
| 31 | `docs/ui-components/PAGES.md` | docs/ui-components/* | 0 | checked, nothing found |
| 32 | `docs/ui-components/README.md` | docs/ui-components/* | 0 | checked, nothing found |
| 33 | `docs/ui-components/REUSE.md` | docs/ui-components/* | 0 | checked, nothing found |
| 34 | `docs/ui-components/TEMPLATE.md` | docs/ui-components/* | 0 | checked, nothing found |
| 35 | `docs/ui-components/bars.md` | docs/ui-components/* | 0 | checked, nothing found |
| 36 | `docs/ui-components/buttons.md` | docs/ui-components/* | 0 | checked, nothing found |
| 37 | `docs/ui-components/carousel.md` | docs/ui-components/* | 0 | checked, nothing found |
| 38 | `docs/ui-components/composites.md` | docs/ui-components/* | 0 | checked, nothing found |
| 39 | `docs/ui-components/core.md` | docs/ui-components/* | 0 | checked, nothing found |
| 40 | `docs/ui-components/display.md` | docs/ui-components/* | 0 | checked, nothing found |
| 41 | `docs/ui-components/env-graph.md` | docs/ui-components/* | 0 | checked, nothing found |
| 42 | `docs/ui-components/env-screens.md` | docs/ui-components/* | 0 | checked, nothing found |
| 43 | `docs/ui-components/filter-display.md` | docs/ui-components/* | 0 | checked, nothing found |
| 44 | `docs/ui-components/filter-screens.md` | docs/ui-components/* | 0 | checked, nothing found |
| 45 | `docs/ui-components/fx-chain.md` | docs/ui-components/* | 0 | checked, nothing found |
| 46 | `docs/ui-components/fx-display.md` | docs/ui-components/* | 0 | checked, nothing found |
| 47 | `docs/ui-components/fx-editor.md` | docs/ui-components/* | 0 | checked, nothing found |
| 48 | `docs/ui-components/fx-gallery.md` | docs/ui-components/* | 0 | checked, nothing found |
| 49 | `docs/ui-components/helpers.md` | docs/ui-components/* | 0 | checked, nothing found |
| 50 | `docs/ui-components/knob-panel.md` | docs/ui-components/* | 0 | checked, nothing found |
| 51 | `docs/ui-components/knob.md` | docs/ui-components/* | 0 | checked, nothing found |
| 52 | `docs/ui-components/led-list.md` | docs/ui-components/* | 0 | checked, nothing found |
| 53 | `docs/ui-components/lfo-parts.md` | docs/ui-components/* | 0 | checked, nothing found |
| 54 | `docs/ui-components/lfo-screens.md` | docs/ui-components/* | 0 | checked, nothing found |
| 55 | `docs/ui-components/list-popup.md` | docs/ui-components/* | 0 | checked, nothing found |
| 56 | `docs/ui-components/load-bar.md` | docs/ui-components/* | 0 | checked, nothing found |
| 57 | `docs/ui-components/macros.md` | docs/ui-components/* | 0 | checked, nothing found |
| 58 | `docs/ui-components/master-screen.md` | docs/ui-components/* | 0 | checked, nothing found |
| 59 | `docs/ui-components/mixer.md` | docs/ui-components/* | 0 | checked, nothing found |
| 60 | `docs/ui-components/mod-screens.md` | docs/ui-components/* | 0 | checked, nothing found |
| 61 | `docs/ui-components/mod-source-board.md` | docs/ui-components/* | 0 | checked, nothing found |
| 62 | `docs/ui-components/modulations-page.md` | docs/ui-components/* | 0 | checked, nothing found |
| 63 | `docs/ui-components/note-mask.md` | docs/ui-components/* | 0 | checked, nothing found |
| 64 | `docs/ui-components/number-field.md` | docs/ui-components/* | 0 | checked, nothing found |
| 65 | `docs/ui-components/osc-screens.md` | docs/ui-components/* | 0 | checked, nothing found |
| 66 | `docs/ui-components/pictogram-selector.md` | docs/ui-components/* | 0 | checked, nothing found |
| 67 | `docs/ui-components/qgroup.md` | docs/ui-components/* | 0 | checked, nothing found |
| 68 | `docs/ui-components/shape-editor.md` | docs/ui-components/* | 0 | checked, nothing found |
| 69 | `docs/ui-components/stepper.md` | docs/ui-components/* | 0 | checked, nothing found |
| 70 | `docs/ui-components/text-entry.md` | docs/ui-components/* | 0 | checked, nothing found |
| 71 | `docs/ui-components/theme.md` | docs/ui-components/* | 0 | checked, nothing found |
| 72 | `docs/ui-components/toggle.md` | docs/ui-components/* | 0 | checked, nothing found |
| 73 | `docs/ui-components/type-bar.md` | docs/ui-components/* | 0 | checked, nothing found |
| 74 | `docs/ui-components/voice-displays.md` | docs/ui-components/* | 0 | checked, nothing found |
| 75 | `docs/ui-components/voice-screen.md` | docs/ui-components/* | 0 | checked, nothing found |
| 76 | `.agents/skills/wavetable-lab/SKILL.md` | .agents/skills/** | 0 | checked, nothing found |
| 77 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/6-szuro-gorbe.md` | .hw-local/mpc-ui-plans/*.md | 1 | **1 finding(s)** |
| 78 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/ESTE-2026-11-18.md` | .hw-local/mpc-ui-plans/*.md | 1 | **1 finding(s)** |
| 79 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/HANDOFF-2026-11-18-coordinator.md` | .hw-local/mpc-ui-plans/*.md | 2 | **2 finding(s)** |
| 80 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/REGGEL-szurogorbe.md` | .hw-local/mpc-ui-plans/*.md | 0 | checked, nothing found |
| 81 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/STATE-2026-11-16-limit.md` | .hw-local/mpc-ui-plans/*.md | 1 | **1 finding(s)** |
| 82 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/STATE-2026-11-17-pause.md` | .hw-local/mpc-ui-plans/*.md | 1 | **1 finding(s)** |
| 83 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/TERV-2026-11-14.md` | .hw-local/mpc-ui-plans/*.md | 3 | **3 finding(s)** |
| 84 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/coverage-gaps.md` | .hw-local/mpc-ui-plans/*.md | 0 | checked, nothing found |
| 85 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/gemini-optimisation-scoreboard.md` | .hw-local/mpc-ui-plans/*.md | 0 | checked, nothing found |
| 86 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/gepi-kapuk-eredmeny.md` | .hw-local/mpc-ui-plans/*.md | 3 | **3 finding(s)** |
| 87 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/hangkeszlet-1-meghallgatasi-lap.md` | .hw-local/mpc-ui-plans/*.md | 0 | checked, nothing found |
| 88 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/hangkeszlet-3-meghallgatasi-lap.md` | .hw-local/mpc-ui-plans/*.md | 0 | checked, nothing found |
| 89 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/inhost-draw-prototype.md` | .hw-local/mpc-ui-plans/*.md | 0 | checked, nothing found |
| 90 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/kerdesek-tervezonek.md` | .hw-local/mpc-ui-plans/*.md | 0 | checked, nothing found |
| 91 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/kozosseg-es-megosztas.md` | .hw-local/mpc-ui-plans/*.md | 0 | checked, nothing found |
| 92 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/layout-lint-report.md` | .hw-local/mpc-ui-plans/*.md | 0 | checked, nothing found |
| 93 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/macro-names-proposed.md` | .hw-local/mpc-ui-plans/*.md | 0 | checked, nothing found |
| 94 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/pad-pressure-presets-proposed.md` | .hw-local/mpc-ui-plans/*.md | 0 | checked, nothing found |
| 95 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/pre-release-format.md` | .hw-local/mpc-ui-plans/*.md | 0 | checked, nothing found |
| 96 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/research-draw-into-host.md` | .hw-local/mpc-ui-plans/*.md | 0 | checked, nothing found |
| 97 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/review-3.md` | .hw-local/mpc-ui-plans/*.md | 0 | checked, nothing found |
| 98 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/review-4.md` | .hw-local/mpc-ui-plans/*.md | 0 | checked, nothing found |
| 99 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/review-5.md` | .hw-local/mpc-ui-plans/*.md | 0 | checked, nothing found |
| 100 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/review-6-notes.md` | .hw-local/mpc-ui-plans/*.md | 0 | checked, nothing found |
| 101 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/review-new-elements-1.md` | .hw-local/mpc-ui-plans/*.md | 0 | checked, nothing found |
| 102 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/review-new-elements-2.md` | .hw-local/mpc-ui-plans/*.md | 0 | checked, nothing found |
| 103 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/review-osc-ui-1.md` | .hw-local/mpc-ui-plans/*.md | 0 | checked, nothing found |
| 104 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/review-osc-ui-2.md` | .hw-local/mpc-ui-plans/*.md | 0 | checked, nothing found |
| 105 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/review-queue.md` | .hw-local/mpc-ui-plans/*.md | 0 | checked, nothing found |
| 106 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/robustness-review.md` | .hw-local/mpc-ui-plans/*.md | 1 | **1 finding(s)** |
| 107 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/stock-animacio-gepi.md` | .hw-local/mpc-ui-plans/*.md | 0 | checked, nothing found |
| 108 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/ui-elem-envelope-lfo.md` | .hw-local/mpc-ui-plans/*.md | 0 | checked, nothing found |
| 109 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/ui-elem-filter.md` | .hw-local/mpc-ui-plans/*.md | 0 | checked, nothing found |
| 110 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/ui-elem-mastervoice.md` | .hw-local/mpc-ui-plans/*.md | 0 | checked, nothing found |
| 111 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/ui-elem-mixfx.md` | .hw-local/mpc-ui-plans/*.md | 0 | checked, nothing found |
| 112 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/ui-elem-modmatrix.md` | .hw-local/mpc-ui-plans/*.md | 0 | checked, nothing found |
| 113 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/ui-elem-protokoll.md` | .hw-local/mpc-ui-plans/*.md | 0 | checked, nothing found |
| 114 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/xl-teszt-lista.md` | .hw-local/mpc-ui-plans/*.md | 0 | checked, nothing found |
| 115 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/xl-tier-analysis.md` | .hw-local/mpc-ui-plans/*.md | 0 | checked, nothing found |
| 116 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-ui-plans/xl-tier-bench.md` | .hw-local/mpc-ui-plans/*.md | 0 | checked, nothing found |
| 117 | `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/MEMORY.md` | Memory files (~/.claude/.../memory/*.md) | 0 | checked, nothing found |
| 118 | `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/agy-worker-model-flash.md` | Memory files (~/.claude/.../memory/*.md) | 0 | checked, nothing found |
| 119 | `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/akai-manual-first.md` | Memory files (~/.claude/.../memory/*.md) | 0 | checked, nothing found |
| 120 | `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/cleanup-own-worker-leftovers.md` | Memory files (~/.claude/.../memory/*.md) | 0 | checked, nothing found |
| 121 | `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/copilot-studio-agents-hungarian.md` | Memory files (~/.claude/.../memory/*.md) | 0 | checked, nothing found |
| 122 | `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/feedback-autonomous-hw-debugging.md` | Memory files (~/.claude/.../memory/*.md) | 0 | checked, nothing found |
| 123 | `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/feedback-build-slots-after-panic.md` | Memory files (~/.claude/.../memory/*.md) | 0 | checked, nothing found |
| 124 | `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/feedback-cost-vs-speed.md` | Memory files (~/.claude/.../memory/*.md) | 0 | checked, nothing found |
| 125 | `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/feedback-find-another-way.md` | Memory files (~/.claude/.../memory/*.md) | 0 | checked, nothing found |
| 126 | `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/feedback-md-tasks-done-then-delete.md` | Memory files (~/.claude/.../memory/*.md) | 0 | checked, nothing found |
| 127 | `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/feedback-never-stop-running-work.md` | Memory files (~/.claude/.../memory/*.md) | 0 | checked, nothing found |
| 128 | `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/feedback-worker-ladder.md` | Memory files (~/.claude/.../memory/*.md) | 0 | checked, nothing found |
| 129 | `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/kick-mobile-fullscreen-only.md` | Memory files (~/.claude/.../memory/*.md) | 0 | checked, nothing found |
| 130 | `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/kick-mpc-local-dev.md` | Memory files (~/.claude/.../memory/*.md) | 0 | checked, nothing found |
| 131 | `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/local-browser-worker-skill.md` | Memory files (~/.claude/.../memory/*.md) | 0 | checked, nothing found |
| 132 | `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/local-models-disk.md` | Memory files (~/.claude/.../memory/*.md) | 0 | checked, nothing found |
| 133 | `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/local-worker-skill.md` | Memory files (~/.claude/.../memory/*.md) | 0 | checked, nothing found |
| 134 | `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/mobilkresz-situation-text-truth.md` | Memory files (~/.claude/.../memory/*.md) | 0 | checked, nothing found |
| 135 | `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/mpc-hardware-exclusive.md` | Memory files (~/.claude/.../memory/*.md) | 0 | checked, nothing found |
| 136 | `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/mpc-key61-3101-setup.md` | Memory files (~/.claude/.../memory/*.md) | 1 | **1 finding(s)** |
| 137 | `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/mpc-own-test-project.md` | Memory files (~/.claude/.../memory/*.md) | 0 | checked, nothing found |
| 138 | `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/mpc-xl-device.md` | Memory files (~/.claude/.../memory/*.md) | 0 | checked, nothing found |
| 139 | `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/proper-develop-moving.md` | Memory files (~/.claude/.../memory/*.md) | 0 | checked, nothing found |
| 140 | `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/proper-divergence-precedence.md` | Memory files (~/.claude/.../memory/*.md) | 0 | checked, nothing found |
| 141 | `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/proper-history-reset.md` | Memory files (~/.claude/.../memory/*.md) | 0 | checked, nothing found |
| 142 | `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/proper-m08-vst3-host.md` | Memory files (~/.claude/.../memory/*.md) | 0 | checked, nothing found |
| 143 | `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/synthyboy-community-release-session.md` | Memory files (~/.claude/.../memory/*.md) | 0 | checked, nothing found |
| 144 | `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/synthyboy-key61-qlink-flicker-known-bug.md` | Memory files (~/.claude/.../memory/*.md) | 0 | checked, nothing found |
| 145 | `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/synthyboy-max-quality.md` | Memory files (~/.claude/.../memory/*.md) | 0 | checked, nothing found |
| 146 | `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/synthyboy-mpc-planner-only.md` | Memory files (~/.claude/.../memory/*.md) | 0 | checked, nothing found |
| 147 | `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/synthyboy-presets-not-release-blocker.md` | Memory files (~/.claude/.../memory/*.md) | 0 | checked, nothing found |
| 148 | `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/synthyboy-release-scope.md` | Memory files (~/.claude/.../memory/*.md) | 0 | checked, nothing found |
| 149 | `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/synthyboy-release-version.md` | Memory files (~/.claude/.../memory/*.md) | 0 | checked, nothing found |
| 150 | `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/synthyboy-unity-gain-no-limiter.md` | Memory files (~/.claude/.../memory/*.md) | 0 | checked, nothing found |
| 151 | `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/synthyboy-xl-first.md` | Memory files (~/.claude/.../memory/*.md) | 0 | checked, nothing found |
| 152 | `/Users/szabi/.claude/projects/-Users-szabi-Documents-Projects-Safian/memory/xcode27-devicehub.md` | Memory files (~/.claude/.../memory/*.md) | 0 | checked, nothing found |
| 153 | `/Users/szabi/.claude/CLAUDE.md` | Owner configuration files (Needs the owner) | 2 | **2 finding(s)** |
| 154 | `/Users/szabi/Documents/Projects/Safian/CLAUDE.md` | Owner configuration files (Needs the owner) | 0 | checked, nothing found |
| 155 | `/Users/szabi/Documents/Projects/CLAUDE.md` | Owner configuration files (Needs the owner) | 0 | checked, nothing found |

---
*End of Instruction & Knowledge Audit (2026-11-19).*
