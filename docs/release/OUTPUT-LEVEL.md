# Output level: the single source of truth (2026-11-20)

> **Összefoglaló a tulajdonosnak (magyarul).** Ez az egyetlen dokumentum, amely a kimeneti szintről szól; minden worker-megbízás erre hivatkozik, a régi jelentések nem érvényesek, ahol ellentmondanak.
> **A tulajdonos végleges döntése (2026-11-20, a T = -4.5 dBFS és a "full / near 0" helyébe lép):**
> 1. **Velocity tracking:** minden presetnél 100% (`master.velocity_track = 100.0`, lerp(1, v^2, t)). Megjegyzés: a padok Full Level funkciója minden leütésre fixen velocity 127-et küld a hardverről (`docs/MPC.md` ~705-717); ezt preset beállítás nem módosítja, a `velocity_track 100` a dinamikus játékot és a padok dinamikáját teszi egységessé.
> 2. **Célszintek:** egy hang csúcsa teljes leütésnél (C3 velocity 127 minta-csúcs) `T = -10.0 dBFS`, egységesen minden kategóriára. Ok: a gyári stock hangszerek headroom-ot tartanak a védelem helyett (Fabric Piano egy hang -10.5 dBFS, 4 hangos akkord -3.7 dBFS, egyetlen gyári hangszer sem vágott le).
> 3. **Akkord korlát:** minden 4-, 6- és 8-hangos akkord (36-48-55-60, 48-55-60-64, 6 hangos, 8 hangos) velocity 127-nél legfeljebb `-0.5 dBFS` lehet limiter nélkül mindkét profilban (megoldás -0.55 dBFS-re).
> 4. **Per-profil szintezés és tierTrimDb:** a szintezés profilfüggően, külön-külön fut Key 61-re (`low`) és XL-re (`high,voices=16,control=economy`). Profilonként $d_p = \min(T - \text{note}_p, -0.55 - \text{maxchord}_p)$. A `master.volume = base + d_high` (az XL értéke); az új felső szintű preset mező `tierTrimDb = d_low - d_high` (-6..+9 dB-re korlátozva), amely csak akkor kerül a `.sbpreset`-be, ha magnitúdója legalább 0.5 dB. Ezt az MPC shell betöltéskor beolvassa és az `OutputStage::setGainDb`-n keresztül KIZÁRÓLAG a Key 61 (low profilú) eszközön érvényesíti a meglévő simított erősítéssel.
> 5. **Hangerő-tartalék:** a felhasználónak lehetősége kell legyen a hangerőt feljebb húzni a `master.volume` potival (-60..+6 dB). Minden preset legalább 5 dB tartalékot tart a +6 dB-ig (`master.volume <= +1.0 dB`).
> 6. **ov42 gyökéroka:** az `e03e09f4` commit még voice-count alpha = 0 mellett szintezett a -1 dBFS akkord-korlát alatt, így az egyes hangok kb. -15.7 dBFS-re estek vissza; az `a9766fc2` beemelte az alpha 0.5-öt, de a `docs/release/VOICE-COUNT-GAIN-SPEC.md` 4. és 9. szakasz 6. lépése szerinti újraszintezés kimaradt.
> 7. **Kiadási megjegyzés a felhasználóknak:** A korábbi projektekben elmentett hangszínek nem migrálnak automatikusan; a felhasználóknak újra be kell tölteniük a preseteket a frissített szintek és a tierTrimDb érvényesítéséhez.

Scope: the output level of the SynthyBoy MPC plugin and its desktop shells: what is decided, what is verified, what is refuted, what is open. If another document (listed in section F) disagrees with this one, this one wins.

## A. Decided rules (the owner's words, dated, latest wins)

1. Same level and behaviour as the stock MPC instruments ("a gyárihoz hasonló működés legyen ... a gyári presetekkel együtt pont egyforma lesz a hangerő"), 2026-11-19.
2. Unity staging: with every level control in the chain at 0 (host track and master faders 0.00 dB) the sound sits at its level on the host track meter, with no limiter and no hidden gain; the shell output stage is unity (0 dB), the whole level lives in the presets' `master.volume` ("minden 0-án áll ... mindenféle limiter nélkül"), 2026-11-19. This is our reading of "minden 0-án áll"; the owner may correct it.
3. Limiter: OFF by default with zero added latency, in every shell. LIMIT is the user's opt-in switch ("ha kell limiter, akkor a userek majd bekapcsolják"); per preset the limiter and an output trim are allowed only where distortion / drive needs them, stored with the preset, 2026-11-19. This reverses the earlier "limiter default on".
4. A preset is usable at once: no fiddling with the volume; a full-velocity hit reads near the top of the track meter and never enters the over-0 segment, 2026-11-19. The tension with rule 1 is open question D1.
5. Follow the general plugin convention once researched (job g94, `docs/review/PRESET-LEVEL-CONVENTIONS-2026-11-19.md`), 2026-11-19.
6. No " *" in any program name the host sees (branch `work/no-asterisk`), 2026-11-19.
7. Simple top-level rule & stock headroom (the owner's latest decision, 2026-11-20; supersedes T = -4.5 dBFS of the same day):
   - **Target single-note peak:** `T = -10.0 dBFS` at velocity 127 across all categories (single note C3 sample peak). Stock instruments keep headroom instead of relying on a guard limiter (e.g. Fabric Piano single note -10.5 dBFS, 4-note chord -3.7 dBFS; no stock instrument clipped).
   - **Full chord limit:** every 4-note (ours 36 48 55 60 and stock voicing 48 55 60 64), 6-note, and 8-note chord at velocity 127 at most `-0.5 dBFS` WITHOUT limiter, across both Key 61 and XL profiles (solve target -0.55 dBFS). With voice-count gain alpha 0.5, chords stay under -0.5 dBFS naturally.
   - **Velocity tracking:** `master.velocity_track = 100.0` (%) for EVERY category and preset (engine law `lerp(1, v^2, t)`). Note on Full Level: pad Full Level sends velocity 127 on every hit (`docs/MPC.md` ~705-717); no preset setting changes hardware pad velocity, velocity_track 100 ensures consistent dynamics.
   - **Per-profile levelling:** `level-to-stock.py solve --rule top` solved separately for Key 61 (`low`) and XL (`high,voices=16,control=economy`).
     Per preset $d_p = \min(T - \text{note}_p, -0.55 - \text{maxchord}_p)$ for each profile $p$.
     `master.volume = base + d_high` (the XL value).
     New top-level preset member `tierTrimDb = d_low - d_high`, clamped to [-6, +9] dB, written into `.sbpreset` only when $|tierTrimDb| \ge 0.5$ dB. Applied via `OutputStage::setGainDb` ONLY on the low-profile device (Key 61) at preset load.
   - **Room to +6 dB volume cap:** The user must be able to raise a sound toward full level with `master.volume` (range -60..+6 dB). After levelling, every preset keeps at least 5 dB of room up to +6 dB (`master.volume <= +1.0 dB`). Internal gain was added to volume-capped presets to enforce this.
   - **Voice-count gain:** engine default voice-count scaling alpha = 0.5 (`G = n_eff ^ -0.5`).
   - **Root cause of ov42:** Commit `e03e09f4` levelled with voice-count alpha 0 under the -1 dBFS chord rule so single notes sat at about -15.7 dBFS; commit `a9766fc2` merged alpha 0.5 and the re-level required by `docs/release/VOICE-COUNT-GAIN-SPEC.md` sections 4 and 9 step 6 was skipped.
   - **Release Note:** Users must reload presets in existing projects; saved projects do not migrate automatically.

## B. Verified facts (each with its evidence)

All device numbers: host track meter peak-hold readout, track fader 0.00 dB, master fader 0.00 dB, velocity as stated, note C3 (48) or the named chord, held about 4 s; the readout was reset by a tap before every run.

**B1. Stock references (REFERENCE ONLY, overridden by owner decision 2026-11-20; peak, dBFS; Key 61 device, 2026-11-19; pictures `.hw-local/mpc-perf/k61-shots/level-stock2-*`).**

| kind (presets measured) | 1 note v127 | 4-note v127 | 1 note v100 | 4-note v100 | 6-note v127 |
|---|---|---|---|---|---|
| Bass (Sub Factory, Bassline; both monophonic) | −9.1 | −9.4 | −9.2 | −9.7 | −10.0 |
| Lead (Mini D; monophonic) | −11.5 | −11.9 | −10.6 | −11.5 | −9.7 |
| Pad (Solina, Studio Strings) | −9.6 | −5.6 | −12.4 | −8.9 | −3.5 |
| Keys (Fabric Piano, Stage Piano) | −10.5 | −3.7 | −18.6 | −12.0 | −3.7 |
| Pluck (Fabric Plucked Steel; Jura Abandon as poly) | −16.2 | −8.1 | −19.9 | −11.7 | −5.0 .. −7.0 |

Hype "Almighty" (first reference): note −10.9, 4-note −6.5. Highest stock peak anywhere: −2.98 dBFS (Solina, 6 notes). No stock instrument clipped; a limiter was not provable; sustained live-bar levels (approximate readings) −20…−30 dB, Solina about −7. The mono references say nothing about chord summing; the Bass/Lead chord targets in `scripts/stock-level-targets.json` are judgement, not measurements.

**B2. XL equals Key 61.** XL device, 2026-11-19 (pictures `.hw-local/mpc-perf/xl-shots/level-xl2-*`): the same ten stock presets within about 0.5 dB of the Key 61 values (for example Hype −10.87 / −6.07, Solina −5.38 / −3.63, Stage Piano −8.97 / −3.02).

**B3. ov40 as installed (+9 dB make-up, no limiter).** Source default of the shell build (the package build log does not record the gain; see C). Offline analysis of all 160 presets (Key 61 tier, chord 36 48 55 60): 156 of 160 chord peaks above 0 dBFS, median chord peak about +10.7 dBFS (XL tier +11.0), single notes +1.3 … +4.5 dBFS. Device XL readings agree (Cosmic Teleport +1.20 / +11.56, Punchy Pluck Bass +6.04 / +11.54, EMS Squelch Bass +2.82 / +12.91, Nylon Zone Pluck +7.23 / +14.98; Dirty Distortion Bass −4.50 in every run). The host meter equals our `output_peak` within 0.2 dB (it is read after the shell stage, `shell-mpc/synthyboy_mpc.cpp:3117-3119` of the ov40 source); host faders are at 0.00 dB. The device output clips at 0 dBFS; the dim meter segment above 0 on the Key 61 is the host's over range.

**B4. Engine facts.** The master knee is linear up to 1.8 and tanh toward 2.0 (`core/engine-own/dsp/master_stage.h`); the old limiter had ceiling −1 dBFS, zero attack, 120 ms release (rejected as squashing). Our 4-note chord peak sits about 9–10 dB above our single-note peak (all four voices start together, first crests add: up to +12 dB); the stock gap is 2–8 dB. Random oscillator phase does not cure it (gap 10.7 → 9.8 dB on ten presets). The XL runs the high profile (`profile: high,voices=16,control=economy`).

**B5. Shared output stage (branch `work/loudness-fix`).** `core/include/synthyboy/output_stage.h`: make-up gain (shared constant), optional 3 ms look-ahead limiter (ceiling −0.5 dBFS), idle path bit exact. Cost: idle 0.07–0.4 µs per block on the Mac. Tests: `run_output_stage_test.sh`, `synthyboy_test_output_stage_presets`, `core/tests/test_output_stage.cpp`.

**B6. Level table summary under the 2026-11-20 decision (`docs/release/OUTPUT-LEVEL-TABLE-2026-11-20.md`).**

- Target single-note peak at velocity 127: `T = -4.50 dBFS`.
- Chord peak ceiling: `<= -0.50 dBFS` without limiter.
- Note peak v127: Key 61 median `-5.70 dBFS` (min -11.32, max -4.45); XL median `-5.09 dBFS` (min -11.32, max -4.45).
- 4-note chord peak: median `-2.18 dBFS`, max `-0.51 dBFS`.
- 6-note chord peak: median `-1.85 dBFS`, max `-0.51 dBFS`.
- 8-note chord peak: median `-1.29 dBFS`, max `-0.50 dBFS`.
- Over -0.50 dBFS: exactly 0 chords across all 160 presets and both profiles.
- Velocity dynamics (v127 vs v64 diff): median `+11.91 dB` (~12 dB expected from v^2 law with track 100%).
- Presets reaching master.volume max (+6.0 dB): 5 presets (Bode Barber Riser, Diode Bite Lead, Diode Saturated Bass, Rising Grain Riser, Spectral Gong Pad).
- Sound set batch 3 (24 presets, total 184 factory presets) is levelled by the same rule: all 184 presets meet T = -10.0 dBFS note target and the -0.5 dBFS chord limit with unity shell gain and voice-count alpha 0.5.

## C. Refuted or corrected claims (do not reuse)

| claim | why it is wrong |
|---|---|
| "Key 61 single note −9.6 dBFS" (first reading) | it was the saved state of Acid Saw Reso from the old project, not an ov40 preset |
| the XL status file shows "low" so the XL runs low | `low,` is a literal label in `describeProfile`; the XL runs the high profile |
| "three inherited failing skin tests" (Gemini) | they pass on both trees; the real new failure was `test_docs_mpc` on the "-1.0 dBFS" text |
| "MPC and desktop are bit-identical / parity" (Gemini) | the desktop default engine differs and the offline render skipped the stage; parity holds only for the shared stage class |
| "ov38 was quiet because −12.5 LUFS is too conservative" | the loudness was never quiet at the peaks; the presets clip, the crest is huge |
| "+18 dB make-up, no limiter" in docs | the shipped default was +9 dB (source) and the new design is unity; texts were stale |
| reviewer's ESTIMATE "single-note median +4.5 dBFS" | measured range is +1.3 … +4.5; the median is lower |
| "the loudest 100 ms" as levelling measure for plucks / keys | it hides the sustain; plucks and keys need the note peak, sustained 0.3–2 s is meaningless for decaying sounds |
| "the ov40 gain of 9 dB is proven from the package build log" | it is from the source defaults only; the log has no gain |
| "limiter default on" | reversed by the owner (rule 3) |
| "the −1.0 dBFS ceiling" in older texts | the stage ceiling is −0.5 dBFS (0.94406088) when the limiter is on |
| "the make-up gain −2.5 dB / −1.0 dB" as final | superseded by unity (rule 2); those values belong to the limiter-on variant |

## D. Open questions, each with the one thing that closes it

1. **Target numbers (single note, 4 / 6 / 8-note chord peak, sustained).** "Full press reads near 0" versus "same as stock" (stock: note −10…−12, 4-note −4…−8). Our reading: the meter sits at the healthy top of its range for a full-velocity hit and never enters over-0; a normal chord stays under 0 without a limiter. Closed by: g94 convention research, then ONE decision on four numbers, then the table of E5.
2. **Keys velocity curve.** Stock pianos drop 8 dB from v127 to v100, ours 2.7 dB; `velocity_track` cannot reach 8 dB. Closed by: an engine velocity-curve or per-preset velocity-to-level modulation (owner decision), then one device comparison.
3. **Chord-to-note ratio.** Ours 9–10 dB, stock 2–8 dB. Closed by: the polyphony-dependent gain prototype (OFF by default) measured offline, then the owner's yes/no.
4. **Pluck / Keys sustain 20 dB under stock.** Sound design (faster decay), not levelling. Closed by: owner listening decision per preset.
5. **Presets that cannot reach their target under the peak rule** (list in the levelling test). Closed by: the fix per preset (trim / limiter on that preset) after D1.
6. **XL output / monitor volume.** Not readable from the host screen. Closed by: the owner reading the knob once.
7. **Sound set batch 3 levelling.** The 24 presets of sound set batch 3 (total 184 factory presets) are levelled by the same rule (T = -10.0 dBFS, chords <= -0.5 dBFS, master.volume <= +1.0 dB).

### D1 detail: the result of the unity / hard-peak run (commit e03e09f4, Key 61 medians, target in brackets)

With the limiter OFF and every 4/6/8-note chord at most −1.0 dBFS: note peak Bass −16.6 (−9.1), Lead −15.7 (−11.5), Pad −14.1 (−9.6), Keys −14.7 (−10.5), Pluck −16.2 (−16.2), FX −14.7 (−11.0); note sustained 0.5–4 dB under target; 65 presets miss the loudness target (worst Game Over Zap −25.7, 808 Boom −20.9, Punchy Pluck Bass −19.1, Sub Drop Boom −13.9, EMS Squelch Bass −13.5 dB). Cause: our chord peak is 9–10 dB above the note peak, stock 2–8 dB. Offline prototype, per-voice gain n^−α (n sounding voices; not in the engine): α 0.25 gives chord−note 6–7.4 dB and 12 presets held back; α 0.5 (1/√n, the rule the engine already uses for unison) gives 2.9–4.4 dB, note peaks within −3…+2.3 dB of stock, 9 presets held back (Keys −4.8 dB and Pluck −17.5 dB short in sustain because they decay). Decision for the owner: accept quieter notes, or adopt the polyphony gain (needs smoothing as voices start/stop and a rule for release tails). Per-preset limiter candidates (measured crest ≥ 15 dB): EMS Squelch Bass, Plucked Lead Tone, Fast Arp Bass. `IOChanged` on a limiter switch is unverified on the device.

## E. Plan from here (one owner per step)

1. Opus worker: finish the unity / limiter-off / per-preset limiter run on `work/loudness-fix` (committed; no new variants).
2. Coordinator: this document committed; superseded docs marked with a pointer (section F).
3. Gemini g94: convention research; coordinator: proposes the four target numbers, owner may object.
4. Opus worker (with Gemini for tables): levelling with the agreed numbers, hard peak rule with the limiter OFF, per-preset limiter list with measured reasons.
5. Coordinator + Gemini runner: merge, ONE build and gates, package ov41, install on both MPCs.
6. Coordinator: the **done test**: a Key 61 and XL host-meter table, ours beside stock, same notes and velocities, LIMIT off, meter never in the over-0 segment, plus pictures; the owner listens next to a stock preset.

## F. Superseded documents (pointers added)

- `.hw-local/mpc-perf/level-analysis/REPORT.md`, `TABLE_A.md` (raw analysis, numbers remain valid as ov40 evidence only);
- `docs/release/LEVELLING-STOCK-2026-11-19.md` (method and per-preset tables of the limiter-on variants);
- the Gemini `docs/review/AUDIO-PRESET-VOLUME-REVIEW-2026-11-19.md` chapter 0 claims;
- the level sections of `docs/release/SOUND-FIXES-1.md`;
- the "Output level" text of `docs/MPC.md` (replaced by a pointer here after the final numbers).
