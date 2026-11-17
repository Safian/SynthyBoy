# Sound fixes 1: levels by a perceptual measure, register of seven presets, Fast Arp Bass

Branch `work/sound-fixes` (base df076188). Written from measurements of the offline engine render (own engine, XL profile `high;voices=16;control=economy`, Key 61 profile `low` as a second column); nothing here was heard. The binaries of the sound lab were built before the pending engine fixes (Ladder feedback sum, comb tuning, FX filter drive): see section 9.

## Magyar összefoglaló a tulajdonosnak

- Mind a 160 preset hangerejét újraállítottam egy percepciós mérték szerint (BS.1770 K-súlyozású hangosság, a leghangosabb 100 ms, kategóriánként a hangszerhez illő játékmódon): a "+6 dB" jegyzeteid (Dirty Distortion, EMS Squelch, Fast Arp, Gritty Square) lejjebb mentek (Dirty -9,4, EMS -4,7, Gritty -6,4 dB; Fast Arp az új patch miatt -19,5), a "halkabb" basszusok (Harmonic Sine, Solid Sub, Sub Duo Octave) 1-4 dB-lel feljebb, Alien Communication kb. +11, Cosmic Teleport +6, Sonar Ping +4 dB.
- Regiszter: Warm Analog Bass, Pulse Width Bass, Cosmic Teleport, Detonation Tail, Space Wind Storm két oktávval lejjebb; Punchy Pluck Bass és Sub Drop Boom egy oktávval feljebb (master transpose). Hallgasd meg őket újra, különösen Warm Analog és Punchy Pluck: a mérés szerint ezek már eddig is pontosan a lenyomott hangon szóltak, ugyanúgy mint a "rendben" jelölt Solid Sub, Moog Ladder; itt a kérésedet teljesítettem, de nem találtam hibát, amit javítani kellene.
- Fast Arp Bass most tényleg arp: egy tartott hang a host tempójában tizenhatodokban jár fel: alaphang, kisterc, kvint, oktáv, minden lépés külön pengetve (a transzport járjon, hogy a tempó-szinkron fusson).
- A "limiter" érzés oka valószínűleg a host: +18 dB-es erősítés mellett a preset-ek 8-hangos akkordja +6..+24 dBFS-ig megy; a javaslat 6 dB-es build-erősítés (lásd 8. rész), ennyivel a hangok és a 3 hangú akkordok jórészt tiszták maradnak.
- Újrahallgatandó először (a legnagyobb hangerő-változás, mind eddig nem hallgatott): Infinite Horizon -17 dB, Deep Sea Ambient -14, Harmonic Cloud -13.5, Pitch Drop 808 -12, Triple Fold Reese -9, Dark Cinema Drone -9, Game Over Zap -9 (a lassan felfutó padek az első 112-ben egy másodpercnél lettek belőve, ezért voltak a teljes kinyílásnál sokkal hangosabbak).
- A többi (nem hallgatott) preset ugyanazt a mérési szabályt kapta; a regisztert csak a hallgatott hétnél változtattam, a többi gyanús esetet (Screaming Sync Lead, Twin Warp Lead, Vocaloid Lead, Drawbar Organ Keys, Wah Pressure Clav) a 6. részben listázom, hogy te döntsd el füllel.

## 1. Commits

1. `Register of seven presets ... Fast Arp Bass ...`: generator inputs (`scripts/gen-factory-presets.py`, `scripts/batch1/fx.py`), regenerated presets and `contentpack.json`, `scripts/measure-register.py`.
2. `Perceptual levelling of all 160 presets ...`: `scripts/loudness.py`, `scripts/measure-loudness.py`, `scripts/measure-peaks.py`, `scripts/factory-preset-loudness.json`, `scripts/factory-preset-levels.json` (all 160), regenerated presets, `scripts/tests/test_loudness.py` (written, not run), `docs/user-guide/factory-sounds.md` (playing ranges and the Fast Arp text).
3. This document.

## 2. The loudness measure

**Implementation** (`scripts/loudness.py`, numpy/scipy, the repository already uses both): ITU-R BS.1770-4 K-weighting (high shelf +4 dB at 1.68 kHz, RLB high-pass at 38 Hz; coefficients computed for 44.1 kHz and checked: a 997 Hz sine of -23 dBFS in both channels reads -23.0 LUFS, 50 Hz reads -27.6, 4 kHz -19.7), 400 ms blocks with 75 % overlap, absolute gate -70 LUFS and relative gate -10 LU for the integrated figure, momentary (400 ms) and short-term (3 s) maxima, and a free window length (`window_max`).

**Gestures** (`scripts/measure-loudness.py`; MPC note names, C3 = MIDI 60, so C1 = 36; velocity 100; every note its own render, 0.3 s of silence before it so that the engine's smoothing of `master.volume` does not add a burst to the first 50 ms of a very quiet preset, see section 10):

| Category | Gesture |
|---|---|
| Bass | single notes C1 G1 C2 G2 C3 (36 43 48 55 60), held 2 s |
| Lead | single notes C3 G3 C4 G4 C5 (60 67 72 79 84), held 1.5 s |
| Pad | three note triads on C3, G3, C4, held until the amp attack is over + 0.4 s (2 s .. 5 s) |
| Keys | the same triads, held 2 s |
| Pluck | a played line: C3, C4, C5, each struck four times 0.5 s apart |
| FX | one held note C3, for the natural length of the sound (a 12 s probe: last 400 ms block within 20 LU of the loudest, 1 .. 8 s) |

**The rule** (deviates from the brief's suggestion "short-term max for percussive, integrated for sustained"; the evidence is below): the loudness of a preset is the **energy mean over the renders of its gesture of the loudest 100 ms window (K-weighted LUFS), `fast_max`**. One target: `fast_max` plus the gesture offset of its category = **-23**, and `master.volume` is moved 1:1 in dB until it is (levelled to within 0.05 dB for all 160; the level follows `master.volume` 1:1 once the first 300 ms are skipped).

Why 100 ms: it is the integration time of the ear for a short sound (loudness grows with duration up to about 100 .. 200 ms and then no longer), so a sustained sound and a plucked one with the same loudest 100 ms count the same. The 3 s short-term window and the gated integrated value measure how much energy there is in the whole gesture: a 120 ms pluck is diluted by 14 dB against a pad. His ear says the plucked and squelchy basses are LOUD (EMS Squelch, Fast Arp: "+6 dB"), which only the short windows reproduce (table below; the short-term and integrated measures give them the wrong sign: +5 and +6).

**Check against his fourteen level notes.** The figure is the correction the measure proposes for the preset as it was on his device (old state, df076188 content, XL profile), against the reference of its category (his "ok" presets); his note as the correction he wants (a "+6 dB louder" is -6):

| measure | Dirty Distortion | EMS Squelch | Fast Arp | Gritty Square | Harmonic Sine | Pulse Width | Scream Filter | Solid Sub | Sub Duo Octave | Sync Growl | Alien | Cosmic Teleport | Sonar Ping | Sub Drop Boom | corr | rms err |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| his ear | -6.0 | -6.0 | -6.0 | -6.0 | +3.0 | +2.5 | +3.0 | +3.0 | +2.0 | +3.0 | +9.0 | +9.0 | +1.0 | +3.0 | | |
| fast_max 100 ms | -9.4 | -4.7 | -6.0 | -6.4 | +0.8 | -0.2 | +0.1 | +1.2 | +3.7 | -0.1 | +10.7 | +3.3 | +4.4 | -1.0 | 0.87 | 2.8 |
| momentary max 400 ms | -9.9 | -0.5 | -0.6 | -6.8 | +0.7 | +0.4 | +0.5 | +1.7 | +3.1 | +0.2 | +10.4 | +3.9 | +4.8 | +2.2 | 0.78 | 3.2 |
| short-term max 3 s | -10.2 | +5.3 | +6.1 | -7.0 | +0.6 | +0.5 | +0.5 | +1.8 | +2.9 | +0.2 | +10.0 | +5.3 | +6.7 | +10.3 | 0.50 | 5.5 |
| integrated | -10.1 | +5.2 | -1.5 | -7.0 | +0.6 | +0.5 | +0.4 | +1.7 | +2.9 | +0.1 | +10.0 | +5.4 | +6.8 | +3.7 | 0.67 | 4.1 |

Reading: `fast_max` has the best fit (correlation 0.87, rms error 2.8 dB) and all signs agree for the percussive / saturated presets (Dirty Distortion, EMS Squelch, Fast Arp, Gritty Square: -9.4, -4.7, -6.0, -6.4 against -6) and for the sub presets (Harmonic Sine, Solid Sub, Sub Duo: +0.8, +1.2, +3.7 against +3, +3, +2) and the FX (Alien +10.7 against +9). Not explained: **Sync Growl Bass, Scream Filter Bass and Pulse Width Bass** (his -3, -3, -2.5; measured -0.1, +0.1, -0.2: within 3 dB but no sign; all three are mid-range single-oscillator basses whose pitch or timbre may have been what he reacted to: Pulse Width is also 2 octaves too high in his ear, and Sync Growl's strongest partial is 2 octaves above the key), **Cosmic Teleport** (+3.3 against +9; his note is confounded by the 2 octave register: the register fix lowers it and the levelling then raises it further, section 5: the final change is +5.7), **Sonar Ping** (+4.4 against +1), **Sub Drop Boom** (-1.0 against +3; also a register note: after the +1 octave the final change is -1.1 dB). **Dirty Distortion** is over-corrected (-9.4 against -6: a saturated bass with a crest factor of 0.6 dB, the 100 ms window cannot be the whole story for it). His ear is the reference: after he listens again the reference lists (`OK` in `measure-loudness.py`) and the offsets can be re-fitted in minutes; they are two lists and six numbers, not a tuned curve.

**Cross-category calibration** (a single target needs a statement of how loud a bass note is against a pad chord): the gestures differ (a single note against a chord) so the same `fast_max` does not mean the same loudness; I did not guess: the offsets are fixed so that what he called "ok" stays where it is (Bass, FX) and the categories he has not heard keep their current average level (Lead, Pad, Keys, Pluck), the levelling inside each category is by the measure. This is open until he has heard them (section 11).

| Category | reference (fast_max, LUFS) | gesture offset dB | reference taken from |
|---|---|---|---|
| Bass | -28.3 | +5.3 | his "ok" presets |
| FX | -26.1 | +3.1 | his "ok" presets |
| Lead | -26.1 | +3.1 | median of the category as it was (not heard yet) |
| Pad | -21.4 | -1.6 | median of the category as it was (not heard yet) |
| Keys | -20.5 | -2.5 | median of the category as it was (not heard yet) |
| Pluck | -22.0 | -1.0 | median of the category as it was (not heard yet) |

## 3. Clipping at +18 dB and his level notes

The renders are the raw engine output (the safety stage `outputLimit` of the engine, identity up to +-1.8, tanh towards +-2, is in them), and the loudness rule is based on these unclipped renders. For each of his fourteen notes, did the gesture of the old state exceed 0 dBFS after +18 dB (peak of the gesture / of a four note chord at velocity 127, in dBFS at +18):

| Preset | gesture peak at +18 | 4-note chord v127 at +18 | gesture clipped? |
|---|---|---|---|
| Dirty Distortion Bass | -1.6 | -1.6 | no |
| EMS Squelch Bass | +0.3 | +13.4 | marginal (+0.3) |
| Fast Arp Bass | +11.3 | +16.5 | **yes** |
| Gritty Square Bass | -5.0 | -0.0 | no |
| Harmonic Sine Bass | -9.2 | +6.1 | no |
| Pulse Width Bass | -8.5 | +7.5 | no |
| Scream Filter Bass | -7.8 | +7.7 | no |
| Solid Sub | -8.8 | +6.1 | no |
| Sub Duo Octave | -10.4 | +5.6 | no |
| Sync Growl Bass | -3.3 | +8.9 | no |
| Alien Communication | -13.8 | +5.1 | no |
| Cosmic Teleport | -10.6 | +7.2 | no |
| Sonar Ping | -9.5 | +7.4 | no |
| Sub Drop Boom | -4.5 | +10.9 | no |

So his single-note level notes are not a clipping artefact (only Fast Arp Bass, of the percussive "+6 dB" four, clips on a single note, EMS marginally; Dirty Distortion and Gritty Square do not), but any chord he played (all four-note chords at velocity 127 are over 0 dBFS except Dirty Distortion and Gritty Square) was clipped by the host. His "+6 dB" for Fast Arp and EMS may be partly the density of that clipping.

## 4. Does his set of notes show a pattern for the presets he has not heard?

**Level.** Measured on the fourteen (table in section 2, features from a C2 / C4 note): the "+6 dB louder" ones are **saturated or percussive-resonant**: Dirty Distortion (distortion 12 dB + filter drive 20, crest factor 0.6 dB: a squashed wave), Gritty Square (distortion 8 dB, crest 0.7 dB), EMS Squelch (resonance 70 %, crest 15 dB, a squelch peak per note) and Fast Arp (a 120 ms pluck, crest 33 dB). The "quieter" ones are **pure or low sines and plain mid-range single oscillators**: Harmonic Sine, Solid Sub, Sub Duo Octave (sub energy 70 % below 120 Hz, spectral centroid 85 .. 130 Hz), plus the FX that are quiet in the measure too (Alien Communication, Cosmic Teleport). K-weighting explains both groups: it takes 2 .. 4 dB off the sub range (50 Hz reads -4.6 dB, 100 Hz -1.8) and adds 4 dB above 2 kHz, so a bright, saturated wave reads louder and a sub sine quieter than its RMS, and the 100 ms window explains the percussive ones (a pluck's 100 ms peak is far above its RMS over a chord hold). The rule derived: *loudness is a short-window K-weighted loudness, not the chord RMS*. It predicts his notes at 0.87 correlation (section 2) and is applied to all 160: the same treatment, no special cases. The effect on the presets he has not heard is as large as on those he has: the largest changes are on slow pads (section 7), whose old levels were set after one second of hold.

**Register.** The seven presets have **no common cause that I can measure**. Six of them (Warm Analog Bass, Punchy Pluck Bass, Cosmic Teleport, Detonation Tail, Space Wind Storm, Sub Drop Boom) are plain single-oscillator patches from the first generator template, no transpose, strongest partial exactly at the played key: **identical in this respect to the presets he accepted** (808 Boom, Solid Sub, Moog Ladder Bass, Acid Saw Reso). Only **Pulse Width Bass** has a measurable cause: the Sync warp (type 1, amount 35 %) puts the strongest partial at the 5th harmonic (+2.3 octaves) and the fundamental 27 dB lower: a missing fundamental. The two hypotheses I tested and which do not hold: *missing fundamental / strong high partial* would predict Sync Growl Bass (+2 octaves, fundamental -46 dB) and Formant Vowel Bass (+1) too, and he rated both without a register complaint; *a template default of no register staging* is true of the six but also of the accepted ones. I therefore did **not** extend a register rule to the unheard presets; the candidates are listed in section 6 for him to judge by ear. A reading that fits all the notes: he plays around C3 .. C4 on the keys, expects a bass or a sub boom two octaves below, and the documented bass ranges (C1 .. C3) are what is out of step: the documented range of the seven presets is corrected in the user guide to the new sounding register. If that is the case, the other bass presets will feel too high to him as well; one global answer would be a different keyboard octave on the device, not seven patch changes.

## 5. Register fixes

Cause found with `scripts/measure-register.py` (the lowest strong partial and the fundamental against the played note, XL profile, C2 / C3 / C4, 2^17 point FFT):

| Preset | Before (lowest strong partial at C2, level of the key's own pitch) | Cause | Change | After |
|---|---|---|---|---|
| Warm Analog Bass | at the key, fundamental +0 dB, centroid 103 Hz | plain saw-to-square, filter 320 Hz, no staging | `master.transpose` -24 | lowest strong partial 2 octaves below the key (at C4 it sounds as it sounded at C2: centroid 103 Hz; at C3 61 Hz), character kept (same waveform, filter, macros) |
| Pulse Width Bass | 2 octaves ABOVE the key at C2 / C3 (strongest 5th harmonic, fundamental -27 dB) | Sync warp, 35 % | `master.transpose` -24 | the sync partial now sits at 1.25 x the key, the missing fundamental is two octaves down; centroid 679 -> 213 Hz |
| Punchy Pluck Bass | at the key; odd harmonics only, cutoff 300 Hz | square through a 300 Hz low pass, pure low register | `master.transpose` +12 | partials 1 octave above the key |
| Cosmic Teleport | at the key, a sine-to-saw sweep | no staging | `master.transpose` -24 | centroid 131 -> 33 Hz at C3; the filter (3500 Hz) is left, the sweep keeps its brightness |
| Detonation Tail | boom at the key, noise through a 6 kHz filter | no staging | `master.transpose` -24 (in `scripts/batch1/fx.py`) | centroid 1018 -> 327 Hz; the boom's pitch fall is relative to the key and still works |
| Space Wind Storm | noise + a saw at the key, resonance at 900 Hz | the pitch of the sound is the filter resonance | `master.transpose` -24 and `filter1.cutoff` 900 -> 225 Hz | centroid 4556 -> 1695 Hz; the howl is two octaves down |
| Sub Drop Boom | a sine at the key | no staging | `master.transpose` +12 | partials 1 octave above the key |

The note on Sub Drop Boom is ambiguous in Hungarian ("1 oktával alcsonyabb"): I read it, as the coordinator did, as "one octave lower than it should be" like the Punchy Pluck note; if he meant "make it one octave lower" it is one number: `master.transpose` -12 instead of +12 in `scripts/gen-factory-presets.py`. Why `master.transpose`: one parameter moves all oscillators, the sub, the keytracked filters and LFOs together, so the patch keeps its character and its macros; it is implemented in the own engine and measured (the partials move exactly -24 / +12 semitones, levels re-measured and re-levelled). The K-weighted loudness of a note falls when it is played lower, so the levelling raised these presets' volume where needed (table in section 7).

## 6. Register scan of all 160 (XL profile, C2 / C3 / C4, lowest strong partial within 20 dB of the loudest peak)

Expectation: Bass the lowest strong partial at or below the key; everything else at the key. **No other plain mistake** was found; everything below was left unchanged for his ear. (Cents: resonances, unison detune, comb and formant peaks pull the strongest-partial estimate by 10 .. 100 cents; that is not a tuning error except the comb ones: Comb Metal Bass -95 cents at C4, Karplus Harp, Comb Zither: the comb tuning fix is pending in the integration session.)

*Bass above the key:* Formant Vowel Bass (+1 octave at C2 / C3: the vowel's formant, rated ok), Sync Growl Bass (+2, hard sync partial, fundamental -46 dB; rated for level only), Pitch Drop 808 (a pitch fall measured on its glide), Pulse Width Bass (fixed above).

*Octave-stacked or sub-octave presets (deliberate, listed for him):* Fat Reese, Ladder FM Bass, Sub Duo Octave, Triple Fold Reese, Reese Dual Phase Bass (sub at -1, bass role); pads Brass Swell Ensemble, Chord Cloud Pad, Lorenz Garden, Mid Side Halo, Spectral Vowel Freeze (sub octave layer, -1), Dark Cinema Drone, Throat Singer Drone (-2, drones); FX Bode Shifter Drone, Noise Riser, Reverse Gong Swell, Spectral Chord Morph, Breath of Machines (shifter / sub / noise, no pitch); Lead Vowel Table Lead (-1, a sub layer); keys Glass Zone Bells, Tempo Gate Chords (-1: sub layer); plucks Stutter Grain Kit, Pentatonic Step Pluck (-2 / -1: kit, step walk), Trance Short Pluck (-1), Harp Drops, Bell Mallet Pluck (a note outside the sample range at C2).

*Above the key (candidates to listen to first):* Screaming Sync Lead (+2, fundamental -51 dB), Twin Warp Lead (+2, -23 dB), Vocaloid Lead (+2 at C2 / C3), Pulse Width Mod Lead (+2 at C4: the same Sync warp as Pulse Width Bass), Wah Pressure Clav (+2 / +1), **Drawbar Organ Keys (+1 octave with the played pitch absent, -84 dB: its first oscillator is transposed +12 on purpose in the data, but a drawbar organ one octave up is a likely mistake)**, Celesta Bells and Starlight Shimmer (+12 on purpose, the bell / shimmer register), Vowel Whistle Lead, Vowel Choir Pad, Formant Pop Pluck, Alien Communication (+1: vowel formant table), Bode Barber Riser (+3 .. +1: a riser). The formant ones are the vowel's own formant, not a transposition.

## 7. Levels of all 160 (XL profile)

Old vol / New vol = `master.volume` before and after; L = calibrated `fast_max` (target -23); peaks in dBFS after the +18 dB make-up of the MPC shell: loudest sample of the gesture, of a four note chord at velocity 127, and the worst case (eight note chord C2 .. G4 at velocity 127). The figures of the old state are those of the content of df076188.

| Preset | Cat | Old vol dB | Old L | New vol dB | Change dB | New L | Peak gesture (+18) | Peak 4-chord v127 (+18) | Peak 8-chord v127 (+18) |
|---|---|---|---|---|---|---|---|---|---|
| 808 Boom | Bass | -9.4 | -18.8 | -13.6 | -4.2 | -23.0 | -6.0 | 8.0 | 13.1 |
| Acid Saw Reso | Bass | -12.0 | -22.4 | -12.6 | -0.6 | -23.0 | -4.8 | 10.6 | 15.1 |
| Bend Formant Growl | Bass | -18.7 | -17.2 | -24.5 | -5.8 | -23.0 | -1.2 | 3.2 | 1.4 |
| Click Attack Bass | Bass | -18.8 | -22.7 | -19.1 | -0.3 | -23.0 | -7.2 | 6.3 | 11.3 |
| Comb Metal Bass | Bass | -13.3 | -22.6 | -13.7 | -0.4 | -23.0 | -2.0 | 9.0 | 15.3 |
| Custom Shape Wobble | Bass | -14.3 | -23.9 | -13.4 | +0.9 | -23.0 | -3.6 | 10.3 | 16.8 |
| Diode Saturated Bass | Bass | -7.0 | -24.3 | -5.7 | +1.3 | -23.0 | -9.1 | 7.0 | 6.3 |
| Dirty Distortion Bass | Bass | -19.6 | -13.6 | -29.0 | -9.4 | -23.0 | -11.0 | -11.0 | -11.0 |
| EMS Squelch Bass | Bass | -7.5 | -18.3 | -12.2 | -4.7 | -23.0 | -4.4 | 8.7 | 15.5 |
| Fast Arp Bass | Bass | 2.9 | -17.0 | -16.6 | -19.5 | -23.0 | -3.0 | 12.5 | 18.4 |
| Fat Reese | Bass | -19.6 | -23.3 | -19.3 | +0.3 | -23.0 | -5.9 | 9.1 | 13.0 |
| Formant Vowel Bass | Bass | -21.6 | -24.3 | -20.3 | +1.3 | -23.0 | -3.9 | 7.1 | 11.5 |
| Gritty Square Bass | Bass | -18.0 | -16.6 | -24.4 | -6.4 | -23.0 | -11.4 | -6.4 | -6.4 |
| Harmonic Sine Bass | Bass | -16.9 | -23.8 | -16.1 | +0.8 | -23.0 | -8.4 | 6.9 | 7.4 |
| Ladder FM Bass | Bass | -17.5 | -21.3 | -19.2 | -1.7 | -23.0 | -3.6 | 7.3 | 5.8 |
| Moog Ladder Bass | Bass | -11.4 | -22.6 | -11.8 | -0.4 | -23.0 | -4.8 | 10.5 | 14.6 |
| Odd Harmonics Bass | Bass | -16.8 | -23.4 | -16.4 | +0.4 | -23.0 | -8.5 | 6.7 | 11.4 |
| Pitch Drop 808 | Bass | -4.6 | -11.4 | -16.2 | -11.6 | -23.0 | -5.9 | -2.1 | 2.4 |
| Pulse Width Bass | Bass | -15.2 | -22.8 | -16.4 | -1.2 | -23.0 | -9.7 | 6.4 | 10.8 |
| Punchy Pluck Bass | Bass | -1.5 | -15.9 | -6.9 | -5.4 | -23.0 | -0.4 | 12.1 | 10.8 |
| Reese Dual Phase Bass | Bass | -23.5 | -15.0 | -31.5 | -8.0 | -23.0 | -5.5 | 0.0 | 0.6 |
| Scream Filter Bass | Bass | -21.0 | -23.1 | -20.9 | +0.1 | -23.0 | -7.7 | 7.8 | 13.5 |
| Solid Sub | Bass | -16.4 | -24.2 | -15.2 | +1.2 | -23.0 | -7.6 | 7.3 | 5.1 |
| Space Wobble Bass | Bass | -14.9 | -23.9 | -14.0 | +0.9 | -23.0 | -6.2 | 7.8 | 13.2 |
| Sub Duo Octave | Bass | -23.7 | -26.7 | -20.0 | +3.7 | -23.0 | -6.7 | 9.3 | 13.7 |
| Subterranean Duo | Bass | -18.7 | -20.6 | -21.1 | -2.4 | -23.0 | -4.3 | 6.5 | 11.1 |
| Sync Growl Bass | Bass | -8.7 | -22.9 | -8.8 | -0.1 | -23.0 | -3.4 | 8.8 | 7.2 |
| Triple Fold Reese | Bass | -14.5 | -13.9 | -23.6 | -9.1 | -23.0 | -3.1 | 0.9 | -0.1 |
| Warm Analog Bass | Bass | -9.7 | -24.7 | -6.2 | +3.5 | -23.0 | -4.2 | 11.4 | 15.6 |
| Alien Communication | FX | -20.8 | -33.7 | -10.1 | +10.7 | -23.0 | -3.1 | 15.8 | 21.0 |
| Bode Barber Riser | FX | -5.5 | -17.9 | -10.6 | -5.1 | -23.0 | -3.6 | -8.0 | -7.0 |
| Bode Shifter Drone | FX | -10.1 | -23.1 | -10.0 | +0.1 | -23.0 | 3.3 | 12.3 | 15.0 |
| Breath of Machines | FX | -19.7 | -21.2 | -21.5 | -1.8 | -23.0 | -2.4 | -1.2 | 2.2 |
| Cosmic Teleport | FX | -17.3 | -26.3 | -11.6 | +5.7 | -23.0 | -4.9 | 10.7 | 14.7 |
| Detonation Tail | FX | -3.3 | -10.6 | -18.3 | -15.0 | -23.0 | -0.9 | -1.9 | -2.8 |
| Game Over Zap | FX | -12.3 | -14.4 | -20.9 | -8.6 | -23.0 | -5.7 | -2.9 | -2.9 |
| Gong Grain Swarm | FX | -6.8 | -24.0 | -5.8 | +1.0 | -23.0 | 0.9 | 10.2 | 10.4 |
| Metal Gong Strike | FX | -19.1 | -23.5 | -18.6 | +0.5 | -23.0 | -3.7 | 9.6 | 12.9 |
| Noise Riser | FX | -16.5 | -19.9 | -19.6 | -3.1 | -23.0 | -4.4 | 7.6 | 11.2 |
| Reverse Gong Swell | FX | -0.8 | -18.4 | -5.4 | -4.6 | -23.0 | 0.4 | 4.5 | 3.3 |
| Rising Grain Riser | FX | 0.0 | -16.9 | -6.1 | -6.1 | -23.0 | -5.0 | -0.8 | -1.9 |
| Sci-Fi Laser Sweep | FX | -14.7 | -25.3 | -12.4 | +2.3 | -23.0 | -4.6 | 11.0 | 16.1 |
| Sonar Ping | FX | -11.5 | -27.4 | -7.1 | +4.4 | -23.0 | -5.1 | 11.8 | 17.0 |
| Space Wind Storm | FX | -16.7 | -19.2 | -20.1 | -3.4 | -23.0 | -6.3 | 6.3 | 11.2 |
| Spectral Chord Morph | FX | -9.9 | -22.1 | -10.8 | -0.9 | -23.0 | -0.9 | 8.9 | 7.1 |
| Sub Drop Boom | FX | -12.1 | -22.0 | -13.2 | -1.1 | -23.0 | -5.5 | 10.0 | 14.0 |
| Vinyl Dust Crackle | FX | -16.4 | -22.9 | -16.5 | -0.1 | -23.0 | -3.0 | 9.8 | 13.3 |
| Accordion Reed | Keys | -21.1 | -24.3 | -19.8 | +1.3 | -23.0 | 7.5 | 12.0 | 14.7 |
| B3 Drawbar Organ | Keys | -17.0 | -25.5 | -14.5 | +2.5 | -23.0 | 2.7 | 8.8 | 13.0 |
| Breakbeat Slice Keys | Keys | -6.7 | -26.2 | -3.5 | +3.2 | -23.0 | 8.0 | 14.4 | 17.6 |
| Celesta Bells | Keys | -6.0 | -20.1 | -8.9 | -2.9 | -23.0 | 6.2 | 12.5 | 17.6 |
| Clavinet Funk | Keys | -10.7 | -16.9 | -16.8 | -6.1 | -23.0 | 1.2 | 1.2 | 1.2 |
| Drawbar Organ Keys | Keys | -18.6 | -24.3 | -17.3 | +1.3 | -23.0 | 5.2 | 11.9 | 15.3 |
| Dream Grand Piano | Keys | -16.4 | -23.0 | -16.4 | +0.0 | -23.0 | 4.8 | 10.9 | 14.7 |
| Dusty Rhodes Keys | Keys | -18.0 | -22.8 | -18.2 | -0.2 | -23.0 | 4.6 | 8.6 | 12.0 |
| Dyno Electric Piano | Keys | -3.8 | -21.9 | -4.9 | -1.1 | -23.0 | 8.5 | 13.3 | 17.6 |
| FM Soft Organ | Keys | -15.1 | -24.6 | -13.5 | +1.6 | -23.0 | 3.3 | 9.5 | 13.6 |
| Glass Organ Tones | Keys | -14.0 | -25.5 | -11.5 | +2.5 | -23.0 | 5.0 | 10.7 | 14.7 |
| Glass Zone Bells | Keys | -12.1 | -25.0 | -10.1 | +2.0 | -23.0 | 6.6 | 14.5 | 19.3 |
| Harpsichord Bar | Keys | -4.1 | -19.8 | -7.3 | -3.2 | -23.0 | 8.8 | 15.4 | 20.4 |
| Just Intonation Keys | Keys | -12.2 | -22.5 | -12.7 | -0.5 | -23.0 | 6.5 | 10.8 | 14.1 |
| Lo-Fi Rhodes | Keys | -12.2 | -23.0 | -12.2 | +0.0 | -23.0 | 6.9 | 11.3 | 12.3 |
| Marimba Layer Keys | Keys | -10.7 | -25.0 | -8.7 | +2.0 | -23.0 | 5.9 | 12.5 | 16.8 |
| Muted Honky Tonk | Keys | -7.6 | -22.1 | -8.5 | -0.9 | -23.0 | 7.6 | 15.1 | 19.7 |
| Rotary Rock Organ | Keys | -22.1 | -20.5 | -24.6 | -2.5 | -23.0 | 5.5 | 6.8 | 7.7 |
| Tempo Gate Chords | Keys | -12.9 | -23.0 | -12.9 | +0.0 | -23.0 | 6.4 | 10.6 | 14.9 |
| Tine Glass Keys | Keys | -17.8 | -26.1 | -14.7 | +3.1 | -23.0 | 5.9 | 12.2 | 17.2 |
| Utility Width Keys | Keys | -16.5 | -22.3 | -17.2 | -0.7 | -23.0 | 7.2 | 12.9 | 15.5 |
| Vibraphone Bars | Keys | -19.0 | -24.3 | -17.7 | +1.3 | -23.0 | 4.2 | 9.0 | 13.4 |
| Vintage Electric Tine | Keys | -17.3 | -23.1 | -17.2 | +0.1 | -23.0 | 8.0 | 12.0 | 15.4 |
| Wah Pressure Clav | Keys | -8.6 | -19.3 | -12.3 | -3.7 | -23.0 | 2.8 | 4.0 | 5.8 |
| Warm Toy Piano | Keys | -6.8 | -20.1 | -9.7 | -2.9 | -23.0 | 5.9 | 12.5 | 17.3 |
| Bell Chime Lead | Lead | -14.0 | -23.8 | -13.2 | +0.8 | -23.0 | -0.3 | 11.1 | 16.4 |
| Bell FM Ripple Lead | Lead | -17.9 | -23.3 | -17.6 | +0.3 | -23.0 | -3.2 | 10.0 | 14.2 |
| Breath Flute Lead | Lead | -13.4 | -19.0 | -17.4 | -4.0 | -23.0 | -4.4 | -0.4 | 0.5 |
| Chorused Saw Solo | Lead | -18.8 | -23.0 | -18.8 | +0.0 | -23.0 | -0.7 | 9.6 | 14.5 |
| Cosmic Drift Lead | Lead | -15.7 | -22.2 | -16.5 | -0.8 | -23.0 | -1.6 | 8.7 | 12.3 |
| Diode Bite Lead | Lead | -7.1 | -25.7 | -4.4 | +2.7 | -23.0 | -7.7 | 8.3 | 14.1 |
| Dirty Peak Lead | Lead | -27.8 | -27.0 | -23.8 | +4.0 | -23.0 | -4.4 | 11.7 | 17.2 |
| Dual Octave Hero | Lead | -19.5 | -23.6 | -18.9 | +0.6 | -23.0 | -3.4 | 9.5 | 15.0 |
| FM Digital Lead | Lead | -15.2 | -23.5 | -14.7 | +0.5 | -23.0 | -7.1 | 8.4 | 12.3 |
| Harmonic Shimmer Lead | Lead | -20.7 | -21.8 | -21.9 | -1.2 | -23.0 | -2.6 | 7.6 | 11.0 |
| Hyper Saw Lead | Lead | -18.5 | -23.4 | -18.1 | +0.4 | -23.0 | -1.0 | 10.0 | 14.7 |
| Laser Beam Lead | Lead | -14.8 | -23.2 | -14.6 | +0.2 | -23.0 | -4.5 | 9.2 | 13.9 |
| MPE Pressure Lead | Lead | -13.5 | -26.5 | -10.0 | +3.5 | -23.0 | -4.1 | 9.1 | 14.1 |
| Metallic Edge Lead | Lead | -13.2 | -20.4 | -15.8 | -2.6 | -23.0 | 0.1 | 2.2 | 2.2 |
| Phaser Sweep Lead | Lead | -14.9 | -26.3 | -11.6 | +3.3 | -23.0 | -2.5 | 8.8 | 14.5 |
| Plucked Lead Tone | Lead | -7.9 | -22.0 | -8.9 | -1.0 | -23.0 | -3.3 | 12.3 | 17.6 |
| Pulse Width Mod Lead | Lead | -6.1 | -22.4 | -6.7 | -0.6 | -23.0 | 3.0 | 11.3 | 17.9 |
| Screaming Sync Lead | Lead | -15.0 | -20.6 | -17.4 | -2.4 | -23.0 | -4.2 | 0.5 | 0.6 |
| Solo Square Lead | Lead | -12.0 | -18.3 | -16.7 | -4.7 | -23.0 | -10.1 | -6.0 | -6.0 |
| Spectral Bowed Glass | Lead | -9.1 | -16.7 | -15.4 | -6.3 | -23.0 | -2.9 | -2.6 | -0.1 |
| Staccato Bleep Lead | Lead | -2.0 | -17.1 | -7.9 | -5.9 | -23.0 | -1.4 | 13.9 | 17.0 |
| Sync Cry Lead | Lead | -11.5 | -21.7 | -12.8 | -1.3 | -23.0 | -3.7 | 10.6 | 14.9 |
| Tri Soft Solo | Lead | -14.2 | -23.9 | -13.3 | +0.9 | -23.0 | -5.0 | 8.3 | 12.1 |
| Triple Fifth Lead | Lead | -14.9 | -23.0 | -14.9 | +0.0 | -23.0 | 0.8 | 9.4 | 13.1 |
| Twin Warp Lead | Lead | -15.7 | -23.3 | -15.4 | +0.3 | -23.0 | -7.4 | 6.9 | 12.7 |
| Vocaloid Lead | Lead | -23.5 | -25.4 | -21.1 | +2.4 | -23.0 | 0.3 | 7.2 | 12.4 |
| Vowel Scrub Choir | Lead | -11.3 | -22.1 | -12.2 | -0.9 | -23.0 | 0.9 | 10.6 | 9.9 |
| Vowel Table Lead | Lead | -9.0 | -16.0 | -16.0 | -7.0 | -23.0 | -2.0 | 2.9 | 1.0 |
| Vowel Whistle Lead | Lead | -11.4 | -14.7 | -19.7 | -8.3 | -23.0 | -3.2 | 1.8 | 0.0 |
| Warm Vintage Lead | Lead | -12.6 | -23.3 | -12.3 | +0.3 | -23.0 | -1.1 | 10.2 | 14.2 |
| Bowed MPE Strings | Pad | -11.2 | -23.3 | -10.9 | +0.3 | -23.0 | 4.5 | 9.8 | 14.9 |
| Bowed Pressure Strings | Pad | -11.5 | -23.8 | -10.7 | +0.8 | -23.0 | 4.9 | 10.5 | 15.3 |
| Brass Swell Ensemble | Pad | -18.1 | -24.7 | -16.4 | +1.7 | -23.0 | 5.5 | 10.2 | 13.9 |
| Cathedral Choir | Pad | -4.4 | -17.3 | -10.1 | -5.7 | -23.0 | 4.6 | 9.8 | 14.4 |
| Chord Cloud Pad | Pad | -6.8 | -24.5 | -5.3 | +1.5 | -23.0 | 4.7 | 10.8 | 11.5 |
| Comb Reso Swell | Pad | -12.3 | -21.9 | -13.4 | -1.1 | -23.0 | 4.9 | 10.9 | 15.3 |
| Dark Cinema Drone | Pad | -8.8 | -13.9 | -17.9 | -9.1 | -23.0 | 4.2 | 7.6 | 9.6 |
| Deep Sea Ambient | Pad | 1.9 | -8.9 | -12.2 | -14.1 | -23.0 | 3.2 | 1.1 | 4.8 |
| Diffusor Cloud Pad | Pad | -14.3 | -23.8 | -13.5 | +0.8 | -23.0 | 4.7 | 10.5 | 15.3 |
| Ethereal Glass | Pad | -4.8 | -16.2 | -11.6 | -6.8 | -23.0 | 5.5 | 11.6 | 15.7 |
| Frozen Aurora | Pad | -8.7 | -15.3 | -16.4 | -7.7 | -23.0 | 4.5 | 7.4 | 10.3 |
| Frozen Pluck Drone | Pad | -7.1 | -22.9 | -7.2 | -0.1 | -23.0 | 5.7 | 9.1 | 13.1 |
| Gentle Flute Pad | Pad | -15.1 | -24.3 | -13.8 | +1.3 | -23.0 | 3.6 | 8.9 | 11.3 |
| Golden Dawn Pad | Pad | -12.5 | -15.8 | -19.7 | -7.2 | -23.0 | 3.3 | 6.9 | 11.4 |
| Harmonic Cloud | Pad | -2.1 | -9.5 | -15.6 | -13.5 | -23.0 | 4.5 | 3.7 | 6.1 |
| Hyper Dimension Pad | Pad | -12.1 | -21.9 | -13.2 | -1.1 | -23.0 | 4.2 | 8.8 | 13.2 |
| Infinite Horizon | Pad | 2.8 | -5.8 | -14.4 | -17.2 | -23.0 | 4.8 | 1.2 | 6.0 |
| Lorenz Garden | Pad | -19.5 | -25.0 | -17.5 | +2.0 | -23.0 | 6.1 | 0.2 | 2.0 |
| Lush Analog Pad | Pad | -18.5 | -21.7 | -19.8 | -1.3 | -23.0 | 5.0 | 12.3 | 16.0 |
| Mid Side Halo | Pad | -13.2 | -21.6 | -14.6 | -1.4 | -23.0 | 6.2 | 9.9 | 11.4 |
| Odd Harmonics Pad | Pad | -20.3 | -23.1 | -20.2 | +0.1 | -23.0 | 4.7 | 8.7 | 12.0 |
| Parallel Dusk Pad | Pad | -24.5 | -25.6 | -21.9 | +2.6 | -23.0 | 4.3 | 10.0 | 12.9 |
| Radiant Pulse Pad | Pad | -17.2 | -23.4 | -16.8 | +0.4 | -23.0 | 5.0 | 11.4 | 15.9 |
| Shepard Ascent | Pad | -6.3 | -23.5 | -5.8 | +0.5 | -23.0 | 6.4 | -1.0 | 1.3 |
| Slow Motion Waves | Pad | -4.7 | -15.1 | -12.6 | -7.9 | -23.0 | 2.7 | 7.9 | 12.8 |
| Space Odyssey Pad | Pad | -11.6 | -18.1 | -16.5 | -4.9 | -23.0 | 4.6 | 8.0 | 11.1 |
| Spectral Gong Pad | Pad | -2.8 | -24.0 | -1.8 | +1.0 | -23.0 | 5.4 | 11.6 | 10.8 |
| Spectral Vowel Freeze | Pad | -13.5 | -24.8 | -11.7 | +1.8 | -23.0 | 5.1 | 11.2 | 11.8 |
| Starlight Shimmer | Pad | -13.1 | -21.9 | -14.2 | -1.1 | -23.0 | 3.1 | 8.8 | 13.7 |
| Stretched Glass Choir | Pad | -20.9 | -24.8 | -19.1 | +1.8 | -23.0 | 4.5 | 9.0 | 14.5 |
| Subtle Tape Drift | Pad | -17.2 | -23.8 | -16.4 | +0.8 | -23.0 | 4.5 | 11.3 | 15.6 |
| Throat Singer Drone | Pad | -11.9 | -22.4 | -12.5 | -0.6 | -23.0 | 5.5 | 7.0 | 8.3 |
| Tri-Band Split Texture | Pad | -19.7 | -23.9 | -18.8 | +0.9 | -23.0 | 5.3 | 9.7 | 12.8 |
| Velvet Strings | Pad | -16.9 | -22.9 | -17.0 | -0.1 | -23.0 | 5.8 | 12.9 | 16.8 |
| Vintage Brass Pad | Pad | -17.3 | -23.8 | -16.5 | +0.8 | -23.0 | 5.7 | 11.4 | 15.6 |
| Vowel Choir Pad | Pad | -18.6 | -24.5 | -17.1 | +1.5 | -23.0 | 3.5 | 8.1 | 12.1 |
| Acoustic Style Pluck | Pluck | -0.5 | -22.6 | -0.9 | -0.4 | -23.0 | 4.6 | 19.4 | 23.5 |
| Aux Keyscale Pluck | Pluck | -4.0 | -22.0 | -5.0 | -1.0 | -23.0 | -0.5 | 12.8 | 16.5 |
| Bell Mallet Pluck | Pluck | -2.9 | -23.2 | -2.7 | +0.2 | -23.0 | 5.3 | 18.6 | 21.9 |
| Chime Drop | Pluck | -5.4 | -24.3 | -4.1 | +1.3 | -23.0 | 2.8 | 18.3 | 21.9 |
| Comb Zither | Pluck | -1.5 | -23.4 | -1.1 | +0.4 | -23.0 | 7.2 | 19.9 | 24.0 |
| Digital Glass Tap | Pluck | 0.2 | -21.8 | -1.0 | -1.2 | -23.0 | 6.5 | 21.0 | 21.4 |
| Electric Mute Pluck | Pluck | -4.3 | -19.5 | -7.8 | -3.5 | -23.0 | 6.9 | 10.2 | 10.2 |
| Formant Pop Pluck | Pluck | -5.6 | -27.9 | -0.7 | +4.9 | -23.0 | 9.1 | 22.1 | 24.0 |
| Glass Ping Pluck | Pluck | -11.8 | -26.9 | -7.9 | +3.9 | -23.0 | 4.1 | 16.0 | 22.4 |
| Harmonic Tine Pluck | Pluck | -4.5 | -22.6 | -4.9 | -0.4 | -23.0 | 2.7 | 17.7 | 20.5 |
| Harp Drops | Pluck | -4.5 | -22.8 | -4.7 | -0.2 | -23.0 | 2.8 | 15.0 | 17.8 |
| Kalimba Tines | Pluck | -7.4 | -23.6 | -6.8 | +0.6 | -23.0 | 4.4 | 17.4 | 22.3 |
| Karplus Harp | Pluck | -3.0 | -17.6 | -8.4 | -5.4 | -23.0 | 0.6 | 0.6 | 0.6 |
| Marimba Synth | Pluck | -1.8 | -22.2 | -2.6 | -0.8 | -23.0 | 4.4 | 17.8 | 20.1 |
| Nylon Zone Pluck | Pluck | -3.0 | -24.6 | -1.4 | +1.6 | -23.0 | 6.3 | 19.9 | 24.0 |
| Pentatonic Step Pluck | Pluck | 0.2 | -22.2 | -0.6 | -0.8 | -23.0 | 9.4 | 9.4 | 9.4 |
| Pizzicato Strings | Pluck | -2.3 | -24.8 | -0.5 | +1.8 | -23.0 | 7.1 | 22.2 | 24.0 |
| Ring Mod Gamelan | Pluck | -7.0 | -23.3 | -6.7 | +0.3 | -23.0 | 1.3 | 12.3 | 17.0 |
| Rubber Bass Pluck | Pluck | -4.2 | -22.0 | -5.2 | -1.0 | -23.0 | 3.9 | 19.0 | 22.7 |
| Spruce Guitar | Pluck | -2.5 | -23.5 | -2.0 | +0.5 | -23.0 | 3.4 | 19.0 | 23.6 |
| Stutter Grain Kit | Pluck | -8.1 | -29.3 | -1.8 | +6.3 | -23.0 | 2.3 | 13.3 | 13.8 |
| Trance Short Pluck | Pluck | 1.2 | -22.5 | 0.7 | -0.5 | -23.0 | 6.3 | 20.1 | 23.4 |

Change statistics: mean -1.3 dB, median -0.1 dB, from -19.5 to +10.7 dB; 51 presets change by more than 3 dB, 23 by more than 6 dB. By category (mean, sd): Bass -2.6 / 5.0, FX -1.4 / 5.5, Keys -0.2 / 2.3, Lead -0.9 / 3.1, Pad -2.2 / 5.0, Pluck +0.3 / 2.5. The largest cuts are the slow pads of the first 112 (the first hundred were levelled after one second of hold, which leaves slow pads far louder once they have opened, as the notes of batch 1 already say), Fast Arp Bass (-19.5: the new patch is far louder than the old pluck), and the saturated basses.

**Where to listen first (the unheard presets with the biggest change; register: none changed for any of them):**

| # | Preset | Category | Level change dB | Register change |
|---|---|---|---|---|
| 1 | Infinite Horizon | Pad | -17.2 | none |
| 2 | Deep Sea Ambient | Pad | -14.1 | none |
| 3 | Harmonic Cloud | Pad | -13.5 | none |
| 4 | Pitch Drop 808 | Bass | -11.6 | none |
| 5 | Triple Fold Reese | Bass | -9.1 | none |
| 6 | Dark Cinema Drone | Pad | -9.1 | none |
| 7 | Game Over Zap | FX | -8.6 | none |
| 8 | Vowel Whistle Lead | Lead | -8.3 | none |
| 9 | Reese Dual Phase Bass | Bass | -8.0 | none |
| 10 | Slow Motion Waves | Pad | -7.9 | none |
| 11 | Frozen Aurora | Pad | -7.7 | none |
| 12 | Golden Dawn Pad | Pad | -7.2 | none |
| 13 | Vowel Table Lead | Lead | -7.0 | none |
| 14 | Ethereal Glass | Pad | -6.8 | none |
| 15 | Spectral Bowed Glass | Lead | -6.3 | none |
| 16 | Stutter Grain Kit | Pluck | +6.3 | none |
| 17 | Clavinet Funk | Keys | -6.1 | none |
| 18 | Rising Grain Riser | FX | -6.1 | none |
| 19 | Staccato Bleep Lead | Lead | -5.9 | none |
| 20 | Cathedral Choir | Pad | -5.7 | none |

**Key 61 (profile `low`, effects skipped) against the XL**, presets whose calibrated loudness differs by more than 2 LU (not levelled for; the XL is the reference):

| Preset | Cat | Key 61 minus XL (LU) |
|---|---|---|
| Reese Dual Phase Bass | Bass | -7.1 |
| Dusty Rhodes Keys | Keys | -5.4 |
| Bode Barber Riser | FX | +3.6 |
| Hyper Dimension Pad | Pad | +3.5 |
| Bode Shifter Drone | FX | +3.3 |
| Cathedral Choir | Pad | +2.5 |
| Lush Analog Pad | Pad | -2.5 |
| Frozen Pluck Drone | Pad | +2.3 |
| Marimba Layer Keys | Keys | -2.1 |

## 8. Peaks, the engine's safety stage and the make-up gain

The engine's own stage `outputLimit` (identity to +-1.8, then tanh towards +-2) acts only on four presets, and only with the worst case, a full eight note chord at velocity 127 or a four note chord (the peak after the stage is 2.0): Formant Pop Pluck (-3.3 dB pulled down), Comb Zither (-1.5), Pizzicato Strings (-1.3), Nylon Zone Pluck (-1.1); on the XL profile no other preset reaches 1.8 raw. So the limiter he hears is **not** the engine's: it is the host, as his -6 dB test shows, hit by a plugin output that is +18 dB hotter than the engine's.

Worst peaks at +18 dB (dBFS): eight note chord v127: Pizzicato Strings, Nylon Zone Pluck, Formant Pop Pluck, Comb Zither +24.0; Spruce Guitar +23.6; Acoustic Style Pluck +23.5; Trance Short Pluck +23.4; Rubber Bass Pluck +22.7 (full table in section 7). Gesture (single notes / three note chords): Pentatonic Step Pluck +9.4, Formant Pop Pluck +9.1, Harpsichord Bar +8.8, Dyno Electric Piano +8.5.

Percentiles over the set of the raw peak (dBFS): single-note gesture median -15.5, 90 % -11.6, max -8.6; four note chord v100 median -12.5, 90 % -6.6, max +0.1; four note chord v127 median -8.4, 90 % -2.5, max +4.2; eight note chord v127 median -4.5, 90 % +2.1, max +6.0.

How many of the 160 presets go over 0 dBFS after a make-up gain (hard clip, no limiter), and the loudness of the set (median of `fast_max` of the gesture, LUFS; the figure after the clip is what comes out of a host without a limiter):

| make-up gain | gesture (single notes / 3-note chords) | 4-note chord v100 | 4-note chord v127 | 8-note chord v127 | set loudness median, unclipped | after the clip |
|---|---|---|---|---|---|---|
| +18 dB | 90 / 160 | 140 | 148 | 151 | -4.1 | -5.4 |
| +12 dB | 23 / 160 | 71 | 133 | 133 | -10.1 | -10.1 |
| +9 dB | 2 / 160 | 25 | 93 | 125 | -13.1 | -13.1 |
| +6 dB | 0 / 160 | 14 | 37 | 102 | -16.1 | -16.1 |
| +3 dB | 0 / 160 | 6 | 19 | 54 | -19.1 | -19.1 |
| +0 dB | 0 / 160 | 1 | 11 | 20 | -22.1 | -22.1 |

(No measurement of a stock MPC instrument's loudness exists in the repository docs, so none is quoted.)

**Recommendation: `SYNTHYBOY_MPC_OUTPUT_GAIN_DB=6`.** At +6 dB no preset exceeds 0 dBFS with the single-note or three-note gesture, 14 exceed with a four-note chord at velocity 100 and 37 at velocity 127: **almost all plucks, bells and electric pianos** (percussive peaks 10 .. 20 dB over their body: Pizzicato Strings +10.2 dBFS, Formant Pop Pluck +10.1, Digital Glass Tap +9.0, Trance Short Pluck +8.1, Comb Zither +7.9, Nylon Zone Pluck +7.9, Acoustic Style Pluck +7.4, Rubber Bass Pluck +7.0, Spruce Guitar +7.0, Bell Mallet Pluck +6.6, Chime Drop +6.3, Marimba Synth +5.8, Harmonic Tine Pluck +5.7, Kalimba Tines +5.4, Glass Ping Pluck +4.0, Alien Communication +3.8, Harpsichord Bar +3.4, Muted Honky Tonk +3.1, Harp Drops +3.0, Glass Zone Bells +2.5, Breakbeat Slice Keys +2.4, Staccato Bleep Lead +1.9, Dyno Electric Piano +1.3, Stutter Grain Kit +1.3, and thirteen more within 1 dB: Utility Width Keys, Velvet Strings, Aux Keyscale Pluck, Celesta Bells, Fast Arp Bass, Marimba Layer Keys, Warm Toy Piano, Bode Shifter Drone, Lush Analog Pad, Plucked Lead Tone, Ring Mod Gamelan, Tine Glass Keys, Punchy Pluck Bass). The set is then 6 dB hotter than the engine's raw output, median loudness -16 LUFS (fast): +9 dB would be 2 presets over for single notes, 25 / 93 for chords; +12 dB, what his -6 dB test corresponds to, has 23 presets over for single notes and 133 for chords. +6 is the highest gain with no clipping for the single-note and three-note playing and a minority only for the hard four-note chord; the transient peaks of those 37 are what an output limiter (`SYNTHYBOY_MPC_OUTPUT_LIMITER=ON`, ceiling -1 dBFS, no latency) is for: with +6 dB and the limiter on, the peaks of the plucks are caught without touching the body of any sound. Without the limiter, +3 dB takes the number of presets over to 19 (v127 chords) and costs 3 dB of loudness.

## 9. What the pending engine fixes could change

To re-measure after the next build (`scripts/measure-loudness.py measure`, then `solve`, `gen-factory-presets.py`): presets that use the Ladder, EMS Ladder or Comb filter, or the FX filter at a mix above 0 (not at the default settings of the sound):

- **Ladder (feedback sum):** Acid Saw Reso, Moog Ladder Bass, Deep Sea Ambient (Ladder); **EMS Squelch Bass (EMS Ladder model 20)**, the one of the fourteen in this class: its -4.7 dB correction was measured before the Ladder fix, re-measure it.
- **Comb tuning:** Comb Metal Bass, Metal Gong Strike, Comb Reso Swell, Comb Zither, Karplus Harp, Breath of Machines (comb on filter 2).
- **FX filter drive:** none of the presets in the table has the FX filter at a mix above 0 by default; Cosmic Teleport, Space Wind Storm and Sub Drop Boom carry it at 0 and open it with Macro 1 only, so their levels, registers and the fourteen notes are not affected at the default settings. Their register fixes (`master.transpose` and the cutoff) are not affected by the fixes.
- Register fixes: none of the seven touches a Ladder, comb or FX filter at its default; no re-measure of the register is needed.

## 10. Notes of the engine found on the way

- The first 50 ms of a render after the preset load are polluted when `master.volume` is far below the default (-12 dB): Dirty Distortion Bass at -47 dB showed a 25 dB louder first 50 ms (the volume smoothing starts from the default). The loudness tool therefore plays 0.3 s after the start; scripts/sound-lab.py's `chord4` level measure (the old tune) does not, and its figure is wrong for a preset set below about -30 dB.
- The sound lab's `level` of batch 1 / 2 and `tune-preset-levels.py` (chord RMS) stay as they are; they measure the old criterion and must not be run on this set: they would undo the levelling. `scripts/measure-loudness.py` is the new tool (`measure`, `calibrate`, `solve`); `scripts/tune-preset-levels.py` is not changed (its tests are pinned).

## 11. Open questions for his ear

1. Warm Analog Bass and Punchy Pluck Bass sit exactly at the key in the engine, like the presets he accepted: after the change, are they where he wants them, and do the other basses (808 Boom, Solid Sub, Moog Ladder Bass ...) now feel too high next to them?
2. Sub Drop Boom: up one octave (done) or down?
3. Lead / Pad / Keys / Pluck: the levels now follow the measure inside the category, the level of a category against the others is as it was; after he has heard them, a list of "ok" presets per category fixes the offsets (`OK` in `scripts/measure-loudness.py`, then `calibrate`).
4. Pads: the slow ones are now 6 .. 17 dB quieter at the opened sustain; that is the bigger change in the whole set.
5. Sync Growl Bass, Scream Filter Bass, Pulse Width Bass: -3 dB by his ear; the measure sees no difference: are they too quiet in a chord, or in a line?
6. Make-up gain 6 dB with or without the output limiter (section 8).

## 12. Each of his notes

| Note | Found | Done |
|---|---|---|
| Dirty Distortion Bass +6 dB | saturated wave (crest 0.6 dB), measure -9.4 | volume -9.4 dB |
| EMS Squelch Bass +6 dB | resonant percussive; measure -4.7 (re-measure after the Ladder fix) | volume -4.7 dB |
| Fast Arp Bass +6 dB, "what makes it an arp?" | nothing: a saw with a 120 ms envelope and no sequencing | rebuilt as a real tempo-synced arpeggio (section 13), volume -19.5 dB for the new patch |
| Gritty Square Bass +6 dB | distorted square, crest 0.7 dB; measure -6.4 | volume -6.4 dB |
| Harmonic Sine Bass -3 dB | sine: K-weighting reads it quieter; measure +0.8 | +0.8 dB |
| Pulse Width Bass -2 .. 3 dB and 2 octaves high | measure -0.2; Sync partial at the 5th harmonic, fundamental -27 dB | `master.transpose` -24, volume -1.2 dB overall |
| Punchy Pluck Bass 1 octave low | at the key, square at 300 Hz | `master.transpose` +12 |
| Scream Filter Bass -3 dB | measure +0.1: not explained | +0.1 dB |
| Solid Sub -3 dB | sine, measure +1.2 | +1.2 dB |
| Sub Duo Octave -2 dB | 70 % below 120 Hz, measure +3.7 | +3.7 dB |
| Sync Growl Bass -3 dB | measure -0.1: not explained | -0.1 dB (see open question 5) |
| Warm Analog Bass 2 octaves high | at the key, as accepted presets; no defect | `master.transpose` -24 |
| Alien Communication -9 dB | measure +10.7 | +10.7 dB |
| Cosmic Teleport -9 dB, 2 octaves high | register fixed, then the measure reads it quieter | `master.transpose` -24, +5.7 dB |
| Detonation Tail 2 octaves high | at the key | `master.transpose` -24 |
| Space Wind Storm 2 octaves high | filter resonance at 900 Hz | `master.transpose` -24 and the cutoff 225 Hz |
| Sonar Ping -1 dB | measure +4.4 | +4.4 dB |
| Sub Drop Boom 1 octave (low) -3 dB | at the key | `master.transpose` +12 (reading as "too low", see section 5), volume -1.1 dB |
| Bode Barber Riser (no verdict) | a riser (+3 .. +1 octave partials, 99 % of the energy above 1.5 kHz) | level only (the measure leaves it where the rule puts it); no register change |

## 13. Fast Arp Bass

The name said "arp", the patch was a saw with a 120 ms envelope and no sequencing (description "Fast sequenced trance arp bass"). The patch now does what the name says, with no rename (the name appears in the categories JSON, the macro-names table, the docs lists and the survey, unchanged): `scripts/gen-factory-presets.py` entry "Fast Arp Bass": LFO 2 with the built-in shape *Steps Up* (four steps) tempo-synced to a quarter note modulates `osc1.transpose` by 12.5 % of its range (12 semitones for the top step) with the scale snap of the Pentatonic Step Pluck (`osc1.transpose_snap` 1193); the four steps therefore play root, minor third, fifth and octave, each a sixteenth note; LFO 3 (*Saw Down*, 1/16 note) plucks every step through the oscillator level (28 % + 45 %) and the cutoff (24 %); the amplitude envelope now sustains (decay 800 ms, sustain 100 %). Measured at 120 BPM, one held key C1: steps at 0.0 / +3.1 / +7.0 / +12.0 semitones, repeated every 0.5 s, each pluck decays inside its sixteenth. The host transport must run for the tempo sync. The macros are unchanged (Cutoff, Drive, Decay + LFO Rate, Detune + Chorus, ..., Reso); the "Decay + LFO Rate" macro moves LFO 1 as before, not the arp LFOs. User guide row and playing range updated (`docs/user-guide/factory-sounds.md`).

## 14. Artefacts the integration session regenerates (nothing was run beyond the generator; no build, no tests)

Source of truth after this branch: `scripts/factory-preset-levels.json` (all 160 levels), `scripts/gen-factory-presets.py` (inputs of Warm Analog Bass, Pulse Width Bass, Punchy Pluck Bass, Cosmic Teleport, Space Wind Storm, Sub Drop Boom and the new Fast Arp Bass; these are in the first 112, `presets` list, the set of 112 names is unchanged, `showcase` still 12), `scripts/batch1/fx.py` (Detonation Tail), `scripts/factory-preset-loudness.json`.

1. **Preset files and pack manifest:** `python3 -I scripts/gen-factory-presets.py` (committed here: `gen-factory-presets.py --check` reports up to date, `check-presets.py` 0 errors); `content/presets/contentpack.json` (SHA-256 of every preset file changes for all 160 files whose volume or content changed: 163 changed files in the tree) is regenerated by the same run. After merging with any other branch that touches presets or the levels file, run it again.
2. **Pinned in the first 112 by the generator tests:** the 112 names (`tune-preset-levels.py` / `test_tune_preset_levels.py` count 112 and the names Acid Saw Reso, 808 Boom: unchanged); the `levels` file now holds a figure for all 160 and overrides `master.volume` of every preset, the first 112 too, the way batch 1 documented (the generator's own `master.volume` entries in the list, e.g. Solid Sub -6, are overridden by the table). Pinned artefacts that change with them: `content/presets/**.sbpreset` of the 112 (levels; Fast Arp Bass, Warm Analog Bass, Pulse Width Bass, Punchy Pluck Bass, Cosmic Teleport, Space Wind Storm, Sub Drop Boom also content), and Detonation Tail of batch 1.
3. **Generated from the presets (run after the merge):** the preset index (`scripts/build-preset-index.py`, in `build-mpc.sh`; test `preset_index_host`), the embedded presets (`scripts/embed-presets.py`, CMake rule on `FACTORY_PRESET_FILES`; `test_embed_presets.py`), the Sounds-browser files (`scripts/gen-xpl-presets.py`, `check-xpl-set.py`; the xpl count stays 161), `docs/release/survey.json` / `survey.md` / `SOUND-SET-SURVEY.md` (`scripts/survey-presets.py`: parameter distances of the eight changed presets), `docs/release/sound-set-batch1-measurements.json` and `SOUND-SET-BATCH-1.md` (Detonation Tail levels and peaks, `gen-sound-set-batch1-doc.py`), the level columns of `SOUND-SET-BATCH-2.md` and `sound-set-batch2-measurements.json`, `docs/ui-design/PRESETS-REPORT.md` / `SWEEP-REPORT.md` (mention Fast Arp Bass and its old description), and `scripts/factory-preset-macro-names.json` (unchanged).
4. **Tests that read the presets:** `scripts/tests/test_check_presets.py` (checks the real presets), `test_embed_presets.py`, `test_gen_xpl_presets.py`, `test_name_preset_macros.py`, `test_tune_preset_levels.py` (pinned 112), the CMake gates of `preset-render` (regenerates and compares the committed presets), `filter-response` (reads `Bass/warm_analog_bass.sbpreset`: transposed now, the curve must still be finite). `scripts/tests/test_loudness.py` is new and not run.
5. **The make-up gain:** the build option `SYNTHYBOY_MPC_OUTPUT_GAIN_DB` (default 18): recommendation 6 (section 8); `docs/MPC.md` line 662 and `docs/release/DEVICE-CHECKLIST.md` line 138 describe the +18 dB stage.
6. **After the next engine build:** the re-measure of section 9 (`measure-loudness.py measure`, `solve`, `gen-factory-presets.py`, repeat to convergence; one round was enough here after the first), and a run of `measure-peaks.py` for the table of section 8.
