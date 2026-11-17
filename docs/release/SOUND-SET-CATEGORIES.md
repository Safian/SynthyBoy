# Sound Set Browser Categories: Review & Mapping

Status: **Proposed for review by the owner.**

This document details the browser category organization for SynthyBoy factory presets in the Akai MPC host Sounds browser. Presets are grouped in a musician-friendly manner into 15 focused sub-styles (maximum 16 groups, all groups contain at least 4 presets).

## Browser Rules & Stock Conventions

- **Category Name Characters**: Stock Akai MPC instruments avoid slashes (`/`) and punctuation in category names, using letters, spaces, digits, and `+` (e.g. `Synth Bass`, `Plucks+Synths`, `Pitched Perc+Bells`). Categories here follow this rule exactly (e.g. `Bass Sub+808`, `Lead Sync+FM`, `Bells+Mallets`, `Strings+Brass`).
- **File Naming & Numbering**: `scripts/gen-xpl-presets.py` names files as `NNN-<Category>-<Preset Name>.xpl`. Files are numbered sequentially (`000` .. `159`) such that presets within each group stay strictly contiguous in the group list order, and presets within each group sort alphabetically.
- **Integrity & Extension**: `scripts/assign-preset-categories.py --check` enforces that all presets exist, all groups meet the 4-preset minimum, and all names are FAT/stock-compliant. `--suggest` provides deterministic recommendations for future sound set additions.

## Group Summary

| # | Group Name | Presets | Origin Folders | Musical Style / Sound Character |
|---|---|---|---|---|
| 01 | **Bass Sub+808** | 6 | Bass | Deep pure sine sub-bass, 808 pitch-drop booms, and low-end foundations |
| 02 | **Bass Reese** | 6 | Bass | Detuned multi-voice saw basslines, wavefolded reeses, and rhythmic wobbles |
| 03 | **Bass Acid** | 6 | Bass | 303-style resonant squelches, diode saturation, distortion, and aggressive growls |
| 04 | **Bass Synth** | 10 | Bass | General analog, punchy transient, hollow funk, and pulse-width basslines |
| 05 | **Lead Synth** | 22 | Lead | Solo anthems, supersaws, expressive analog, and vocal formant leads |
| 06 | **Lead Sync+FM** | 6 | Lead | Hard-sync pitched oscillators, frequency-modulated digital leads, and warp cutters |
| 07 | **Pad Warm** | 10 | Pad | Classic analog strings, warm choirs, vintage brass swells, and lush chord beds |
| 08 | **Pad Evolving** | 17 | Pad | Slow morphing wavetables, ambient diffusor washes, and shifting spectral clouds |
| 09 | **Keys** | 17 | Keys | Acoustic grands, Rhodes/dyno electric pianos, drawbar/rock organs, and clavs |
| 10 | **Pluck** | 15 | Lead, Pluck | Acoustic guitars, Karplus-Strong physical strings, zithers, and transient plucks |
| 11 | **Bells+Mallets** | 11 | Keys, Lead, Pluck | Tuned percussion, celestas, vibraphones, marimbas, kalimbas, and struck gamelans |
| 12 | **Strings+Brass** | 8 | Keys, Pad, Pluck | Orchestral string ensembles, brass sections, accordions, and baroque harpsichords |
| 13 | **Sequence** | 5 | Bass, Keys, Pad, Pluck | Self-playing 16-step pentatonic grooves, tempo-synced chord gates, and fast arps |
| 14 | **Texture** | 8 | FX, Pad | Continuous atmospheric noise beds, vinyl crackle, drones, and ambient windscapes |
| 15 | **FX** | 13 | FX | Transitional risers, pitch sweeps, teleport zaps, impacts, and one-shot effects |

---

## Presets by Group

### 1. Bass Sub+808 (6 presets)

*Deep pure sine sub-bass, 808 pitch-drop booms, and low-end foundations*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 000 | **808 Boom** | `Bass` | Pitch-gliding sine sub bass with classic 808 decay | `000-Bass Sub+808-808 Boom.xpl` |
| 001 | **Harmonic Sine Bass** | `Bass` | Smooth sine sub bass with subtle upper harmonics | `001-Bass Sub+808-Harmonic Sine Bass.xpl` |
| 002 | **Pitch Drop 808** | `Bass` | Sub 808 with rapid envelope pitch drop and pulse knock | `002-Bass Sub+808-Pitch Drop 808.xpl` |
| 003 | **Solid Sub** | `Bass` | Deep pure sine sub bass foundation | `003-Bass Sub+808-Solid Sub.xpl` |
| 004 | **Sub Duo Octave** | `Bass` | Dual octave stacked pure sine sub bass | `004-Bass Sub+808-Sub Duo Octave.xpl` |
| 005 | **Subterranean Duo** | `Bass` | Pure sine sub paired with folded saw growl layer | `005-Bass Sub+808-Subterranean Duo.xpl` |

### 2. Bass Reese (6 presets)

*Detuned multi-voice saw basslines, wavefolded reeses, and rhythmic wobbles*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 006 | **Custom Shape Wobble** | `Bass` | Detuned dual-saw bass with rhythmic hand-drawn wobble LFO | `006-Bass Reese-Custom Shape Wobble.xpl` |
| 007 | **Fat Reese** | `Bass` | 5-voice detuned sawtooth Reese bass for drum and bass | `007-Bass Reese-Fat Reese.xpl` |
| 008 | **Reese Dual Phase Bass** | `Bass` | Dual-phase reese bass using wavetables | `008-Bass Reese-Reese Dual Phase Bass.xpl` |
| 009 | **Scream Filter Bass** | `Bass` | 3-voice detuned saw bass driven through scream filter | `009-Bass Reese-Scream Filter Bass.xpl` |
| 010 | **Space Wobble Bass** | `Bass` | Dub-style wobble bass with LFO-modulated lowpass cutoff | `010-Bass Reese-Space Wobble Bass.xpl` |
| 011 | **Triple Fold Reese** | `Bass` | 3-oscillator detuned saw reese with sine-fold wavefolding | `011-Bass Reese-Triple Fold Reese.xpl` |

### 3. Bass Acid (6 presets)

*303-style resonant squelches, diode saturation, distortion, and aggressive growls*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 012 | **Acid Saw Reso** | `Bass` | Classic 303-style resonant sawtooth acid line | `012-Bass Acid-Acid Saw Reso.xpl` |
| 013 | **Bend Formant Growl** | `Bass` | Aggressive bent saw growl through parallel formant and analog filters | `013-Bass Acid-Bend Formant Growl.xpl` |
| 014 | **Diode Saturated Bass** | `Bass` | Warm saturated diode ladder filter resonant bass | `014-Bass Acid-Diode Saturated Bass.xpl` |
| 015 | **Dirty Distortion Bass** | `Bass` | Overdriven industrial bass using dirty filter model | `015-Bass Acid-Dirty Distortion Bass.xpl` |
| 016 | **EMS Squelch Bass** | `Bass` | Resonant EMS ladder bass with short snappy filter envelope | `016-Bass Acid-EMS Squelch Bass.xpl` |
| 017 | **Sync Growl Bass** | `Bass` | Hard-sync modulated aggressive growl bass | `017-Bass Acid-Sync Growl Bass.xpl` |

### 4. Bass Synth (10 presets)

*General analog, punchy transient, hollow funk, and pulse-width basslines*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 018 | **Click Attack Bass** | `Bass` | Punchy synth bass with transient click attack | `018-Bass Synth-Click Attack Bass.xpl` |
| 019 | **Comb Metal Bass** | `Bass` | Physical resonant comb filter metallic string bass | `019-Bass Synth-Comb Metal Bass.xpl` |
| 020 | **Formant Vowel Bass** | `Bass` | Formant filter sweeping vocal vowel bassline | `020-Bass Synth-Formant Vowel Bass.xpl` |
| 021 | **Gritty Square Bass** | `Bass` | Raw edgy lo-fi square wave synth bass | `021-Bass Synth-Gritty Square Bass.xpl` |
| 022 | **Ladder FM Bass** | `Bass` | FM bass with ladder filter | `022-Bass Synth-Ladder FM Bass.xpl` |
| 023 | **Moog Ladder Bass** | `Bass` | Vintage analog ladder filter round bass | `023-Bass Synth-Moog Ladder Bass.xpl` |
| 024 | **Odd Harmonics Bass** | `Bass` | Hollow funk bass driven by odd harmonic series | `024-Bass Synth-Odd Harmonics Bass.xpl` |
| 025 | **Pulse Width Bass** | `Bass` | PWM modulated pulse wave electro bass | `025-Bass Synth-Pulse Width Bass.xpl` |
| 026 | **Punchy Pluck Bass** | `Bass` | Tight transient bass with short percussive decay | `026-Bass Synth-Punchy Pluck Bass.xpl` |
| 027 | **Warm Analog Bass** | `Bass` | Classic deep house warm analog lowpass bass | `027-Bass Synth-Warm Analog Bass.xpl` |

### 5. Lead Synth (22 presets)

*Solo anthems, supersaws, expressive analog, and vocal formant leads*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 028 | **Breath Flute Lead** | `Lead` | Breath-based flute lead using sample oscillator | `028-Lead Synth-Breath Flute Lead.xpl` |
| 029 | **Chorused Saw Solo** | `Lead` | Bright sawtooth solo lead bathed in stereo chorus | `029-Lead Synth-Chorused Saw Solo.xpl` |
| 030 | **Cosmic Drift Lead** | `Lead` | Slowly drifting ambient analog solo synth lead | `030-Lead Synth-Cosmic Drift Lead.xpl` |
| 031 | **Diode Bite Lead** | `Lead` | Sharp biting diode ladder filtered aggressive lead | `031-Lead Synth-Diode Bite Lead.xpl` |
| 032 | **Dirty Peak Lead** | `Lead` | Distorted resonant peak filter industrial synth lead | `032-Lead Synth-Dirty Peak Lead.xpl` |
| 033 | **Dual Octave Hero** | `Lead` | Heroic stacked octave dual sawtooth anthem lead | `033-Lead Synth-Dual Octave Hero.xpl` |
| 034 | **Harmonic Shimmer Lead** | `Lead` | Upper harmonic rich shimmering solo synth lead | `034-Lead Synth-Harmonic Shimmer Lead.xpl` |
| 035 | **Hyper Saw Lead** | `Lead` | 7-voice detuned supersaw stadium anthem lead | `035-Lead Synth-Hyper Saw Lead.xpl` |
| 036 | **Metallic Edge Lead** | `Lead` | Aggressive electro lead with metallic overtone edge | `036-Lead Synth-Metallic Edge Lead.xpl` |
| 037 | **MPE Pressure Lead** | `Lead` | Expressive MPE solo lead with per-note pressure filter modulation | `037-Lead Synth-MPE Pressure Lead.xpl` |
| 038 | **Phaser Sweep Lead** | `Lead` | Spacey multi-stage phaser swept solo synth lead | `038-Lead Synth-Phaser Sweep Lead.xpl` |
| 039 | **Pulse Width Mod Lead** | `Lead` | Classic 80s chorus-widened pulse-width modulated lead | `039-Lead Synth-Pulse Width Mod Lead.xpl` |
| 040 | **Solo Square Lead** | `Lead` | Expressive monophonic retro square wave solo lead | `040-Lead Synth-Solo Square Lead.xpl` |
| 041 | **Spectral Bowed Glass** | `Lead` | Spectral bowed glass lead | `041-Lead Synth-Spectral Bowed Glass.xpl` |
| 042 | **Staccato Bleep Lead** | `Lead` | Retro arcade-inspired punchy staccato bleep melody lead | `042-Lead Synth-Staccato Bleep Lead.xpl` |
| 043 | **Tri Soft Solo** | `Lead` | Flute-like soft triangle wave solo lead | `043-Lead Synth-Tri Soft Solo.xpl` |
| 044 | **Triple Fifth Lead** | `Lead` | Stacked 3-oscillator power-chord lead with fifth and octave layers | `044-Lead Synth-Triple Fifth Lead.xpl` |
| 045 | **Vocaloid Lead** | `Lead` | Singing formant vocal tract lead synthesizer | `045-Lead Synth-Vocaloid Lead.xpl` |
| 046 | **Vowel Scrub Choir** | `Lead` | Formant-scrubbing choir lead | `046-Lead Synth-Vowel Scrub Choir.xpl` |
| 047 | **Vowel Table Lead** | `Lead` | Vocal vowel lead using wavetables | `047-Lead Synth-Vowel Table Lead.xpl` |
| 048 | **Vowel Whistle Lead** | `Lead` | Mono legato vocal formant morph lead with expressive vibrato | `048-Lead Synth-Vowel Whistle Lead.xpl` |
| 049 | **Warm Vintage Lead** | `Lead` | Smooth vintage analog lowpass filtered solo lead | `049-Lead Synth-Warm Vintage Lead.xpl` |

### 6. Lead Sync+FM (6 presets)

*Hard-sync pitched oscillators, frequency-modulated digital leads, and warp cutters*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 050 | **Bell FM Ripple Lead** | `Lead` | Sine carrier phase-modulated FM bell lead with velocity bite | `050-Lead Sync+FM-Bell FM Ripple Lead.xpl` |
| 051 | **FM Digital Lead** | `Lead` | Crisp digital FM synthesis lead with sharp modulation | `051-Lead Sync+FM-FM Digital Lead.xpl` |
| 052 | **Laser Beam Lead** | `Lead` | Futuristic cutting sync and warp modulation laser lead | `052-Lead Sync+FM-Laser Beam Lead.xpl` |
| 053 | **Screaming Sync Lead** | `Lead` | Aggressive pitched oscillator hard-sync rock lead | `053-Lead Sync+FM-Screaming Sync Lead.xpl` |
| 054 | **Sync Cry Lead** | `Lead` | Expressive singing hard-sync saw lead with envelope sweep | `054-Lead Sync+FM-Sync Cry Lead.xpl` |
| 055 | **Twin Warp Lead** | `Lead` | Dual sync and bend warp stages in series with LFO sweep | `055-Lead Sync+FM-Twin Warp Lead.xpl` |

### 7. Pad Warm (10 presets)

*Classic analog strings, warm choirs, vintage brass swells, and lush chord beds*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 056 | **Cathedral Choir** | `Pad` | Vast reverberant sacred choir pad | `056-Pad Warm-Cathedral Choir.xpl` |
| 057 | **Gentle Flute Pad** | `Pad` | Soft breathy woodwind flute ensemble pad | `057-Pad Warm-Gentle Flute Pad.xpl` |
| 058 | **Golden Dawn Pad** | `Pad` | Peaceful warm morning dawn analog pad | `058-Pad Warm-Golden Dawn Pad.xpl` |
| 059 | **Hyper Dimension Pad** | `Pad` | Sine-to-saw pad expanded with Hyper/Dimension width | `059-Pad Warm-Hyper Dimension Pad.xpl` |
| 060 | **Lush Analog Pad** | `Pad` | Warm enveloping classic analog string/synth pad | `060-Pad Warm-Lush Analog Pad.xpl` |
| 061 | **Mid Side Halo** | `Pad` | 3-oscillator warm pad with mid/side processing and wide halo | `061-Pad Warm-Mid Side Halo.xpl` |
| 062 | **Odd Harmonics Pad** | `Pad` | Warm hollow wooden pipe organ pad with odd harmonics | `062-Pad Warm-Odd Harmonics Pad.xpl` |
| 063 | **Starlight Shimmer** | `Pad` | Bright crystalline shimmering high-register synth pad | `063-Pad Warm-Starlight Shimmer.xpl` |
| 064 | **Subtle Tape Drift** | `Pad` | Vintage warm pad with subtle tape flutter and pitch drift | `064-Pad Warm-Subtle Tape Drift.xpl` |
| 065 | **Vowel Choir Pad** | `Pad` | Synthetic choral pad with warm formant vowel blending | `065-Pad Warm-Vowel Choir Pad.xpl` |

### 8. Pad Evolving (17 presets)

*Slow morphing wavetables, ambient diffusor washes, and shifting spectral clouds*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 066 | **Chord Cloud Pad** | `Pad` | Granular cloud pad with four-chord progression drift | `066-Pad Evolving-Chord Cloud Pad.xpl` |
| 067 | **Comb Reso Swell** | `Pad` | Acoustic-like resonant comb filter swelling wash | `067-Pad Evolving-Comb Reso Swell.xpl` |
| 068 | **Deep Sea Ambient** | `Pad` | Submerged oceanic atmosphere with slow filter movement | `068-Pad Evolving-Deep Sea Ambient.xpl` |
| 069 | **Diffusor Cloud Pad** | `Pad` | Detuned saw pad smeared by Diffusor filter into slow drift | `069-Pad Evolving-Diffusor Cloud Pad.xpl` |
| 070 | **Ethereal Glass** | `Pad` | Airy crystalline glass texture with slowly shifting overtones | `070-Pad Evolving-Ethereal Glass.xpl` |
| 071 | **Frozen Aurora** | `Pad` | Cold crystalline northern lights atmospheric soundscape | `071-Pad Evolving-Frozen Aurora.xpl` |
| 072 | **Frozen Pluck Drone** | `Pad` | Frozen granular pluck drone | `072-Pad Evolving-Frozen Pluck Drone.xpl` |
| 073 | **Harmonic Cloud** | `Pad` | Dense evolving harmonic cluster pad with subtle modulation | `073-Pad Evolving-Harmonic Cloud.xpl` |
| 074 | **Infinite Horizon** | `Pad` | Massive cinematic landscape pad with endless evolving decay | `074-Pad Evolving-Infinite Horizon.xpl` |
| 075 | **Lorenz Garden** | `Pad` | Generative bell pad with chaotic Lorenz and Rossler LFO drift | `075-Pad Evolving-Lorenz Garden.xpl` |
| 076 | **Parallel Dusk Pad** | `Pad` | Dual parallel stereo filters with opposing LFO cutoff motion | `076-Pad Evolving-Parallel Dusk Pad.xpl` |
| 077 | **Slow Motion Waves** | `Pad` | Slow morphing wavetable sweep across spectral frames | `077-Pad Evolving-Slow Motion Waves.xpl` |
| 078 | **Space Odyssey Pad** | `Pad` | Expansive sci-fi atmospheric pad with cosmic movement | `078-Pad Evolving-Space Odyssey Pad.xpl` |
| 079 | **Spectral Gong Pad** | `Pad` | Spectral gong pad | `079-Pad Evolving-Spectral Gong Pad.xpl` |
| 080 | **Spectral Vowel Freeze** | `Pad` | Spectral vowel freezing pad | `080-Pad Evolving-Spectral Vowel Freeze.xpl` |
| 081 | **Stretched Glass Choir** | `Pad` | Spectral-stretched partials drifting across random LFOs | `081-Pad Evolving-Stretched Glass Choir.xpl` |
| 082 | **Throat Singer Drone** | `Pad` | Throat singer drone | `082-Pad Evolving-Throat Singer Drone.xpl` |

### 9. Keys (17 presets)

*Acoustic grands, Rhodes/dyno electric pianos, drawbar/rock organs, and clavs*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 083 | **B3 Drawbar Organ** | `Keys` | Warm tonewheel jazz drawbar organ | `083-Keys-B3 Drawbar Organ.xpl` |
| 084 | **Breakbeat Slice Keys** | `Keys` | Breakbeat-sliced sample keys with parallel crush | `084-Keys-Breakbeat Slice Keys.xpl` |
| 085 | **Clavinet Funk** | `Keys` | Funky 70s electronic clavinet keyboard | `085-Keys-Clavinet Funk.xpl` |
| 086 | **Drawbar Organ Keys** | `Keys` | Drawbar organ emulation using wavetable oscillator | `086-Keys-Drawbar Organ Keys.xpl` |
| 087 | **Dream Grand Piano** | `Keys` | Acoustic grand piano style dynamic keyboard | `087-Keys-Dream Grand Piano.xpl` |
| 088 | **Dusty Rhodes Keys** | `Keys` | Dusty keys emulation with sampled Rhodes | `088-Keys-Dusty Rhodes Keys.xpl` |
| 089 | **Dyno Electric Piano** | `Keys` | Bright modified 80s dyno tine electric piano | `089-Keys-Dyno Electric Piano.xpl` |
| 090 | **FM Soft Organ** | `Keys` | Smooth gospel tonewheel organ using FM synthesis | `090-Keys-FM Soft Organ.xpl` |
| 091 | **Glass Organ Tones** | `Keys` | Crystalline cathedral glass church organ | `091-Keys-Glass Organ Tones.xpl` |
| 092 | **Just Intonation Keys** | `Keys` | Pure harmonic-series keyboard tuned in just intonation | `092-Keys-Just Intonation Keys.xpl` |
| 093 | **Lo-Fi Rhodes** | `Keys` | Mellow chillhop Rhodes electric piano with warm tape saturation | `093-Keys-Lo-Fi Rhodes.xpl` |
| 094 | **Muted Honky Tonk** | `Keys` | Saloon-style upright piano with detuned unison tines | `094-Keys-Muted Honky Tonk.xpl` |
| 095 | **Rotary Rock Organ** | `Keys` | Gritty rock organ through rotary Leslie speaker chorus | `095-Keys-Rotary Rock Organ.xpl` |
| 096 | **Tine Glass Keys** | `Keys` | Electric piano combining FM sine body with glassy bell tine ping | `096-Keys-Tine Glass Keys.xpl` |
| 097 | **Utility Width Keys** | `Keys` | Chorused triangle and square keys with mono low-end utility | `097-Keys-Utility Width Keys.xpl` |
| 098 | **Vintage Electric Tine** | `Keys` | Classic warm Rhodes electric piano with soft tines | `098-Keys-Vintage Electric Tine.xpl` |
| 099 | **Wah Pressure Clav** | `Keys` | Expressive funk clavinet with pad pressure sweeping wah filter | `099-Keys-Wah Pressure Clav.xpl` |

### 10. Pluck (15 presets)

*Acoustic guitars, Karplus-Strong physical strings, zithers, and transient plucks*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 100 | **Acoustic Style Pluck** | `Pluck` | Crisp acoustic guitar transient steel-string pluck | `100-Pluck-Acoustic Style Pluck.xpl` |
| 101 | **Aux Keyscale Pluck** | `Pluck` | Plucked synth with keyboard-scaled dynamic filter sweep | `101-Pluck-Aux Keyscale Pluck.xpl` |
| 102 | **Comb Zither** | `Pluck` | Physical modeled plucked zither string using comb resonance | `102-Pluck-Comb Zither.xpl` |
| 103 | **Digital Glass Tap** | `Pluck` | Modern UI glassy transient percussive tap tone | `103-Pluck-Digital Glass Tap.xpl` |
| 104 | **Electric Mute Pluck** | `Pluck` | Palm-muted electric guitar style synth pluck | `104-Pluck-Electric Mute Pluck.xpl` |
| 105 | **Formant Pop Pluck** | `Pluck` | Short staccato vocal formant pop pluck | `105-Pluck-Formant Pop Pluck.xpl` |
| 106 | **Glass Ping Pluck** | `Pluck` | Glassy plucked sound using sampled glass | `106-Pluck-Glass Ping Pluck.xpl` |
| 107 | **Harmonic Tine Pluck** | `Pluck` | Rich harmonic struck tine transient pluck | `107-Pluck-Harmonic Tine Pluck.xpl` |
| 108 | **Harp Drops** | `Pluck` | Fluid crystalline concert harp plucked string drops | `108-Pluck-Harp Drops.xpl` |
| 109 | **Karplus Harp** | `Pluck` | Karplus-Strong physical string model excited by noise burst | `109-Pluck-Karplus Harp.xpl` |
| 110 | **Nylon Zone Pluck** | `Pluck` | Nylon plucked multisample | `110-Pluck-Nylon Zone Pluck.xpl` |
| 111 | **Rubber Bass Pluck** | `Pluck` | Bouncy rubberized synth pluck with elastic filter response | `111-Pluck-Rubber Bass Pluck.xpl` |
| 112 | **Spruce Guitar** | `Pluck` | Clean acoustic guitar pluck with warm spruce resonance | `112-Pluck-Spruce Guitar.xpl` |
| 113 | **Stutter Grain Kit** | `Pluck` | Stutter grain drum kit | `113-Pluck-Stutter Grain Kit.xpl` |
| 114 | **Trance Short Pluck** | `Pluck` | Punchy sequenced trance groove pluck with tight decay | `114-Pluck-Trance Short Pluck.xpl` |

### 11. Bells+Mallets (11 presets)

*Tuned percussion, celestas, vibraphones, marimbas, kalimbas, and struck gamelans*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 115 | **Bell Chime Lead** | `Lead` | Crystalline bell overtones and struck chime lead | `115-Bells+Mallets-Bell Chime Lead.xpl` |
| 116 | **Bell Mallet Pluck** | `Pluck` | Percussive bell mallet strike with metallic overtones | `116-Bells+Mallets-Bell Mallet Pluck.xpl` |
| 117 | **Celesta Bells** | `Keys` | Fairytale orchestral celesta bell keyboard | `117-Bells+Mallets-Celesta Bells.xpl` |
| 118 | **Chime Drop** | `Pluck` | Delicate falling metallic glass chime cluster | `118-Bells+Mallets-Chime Drop.xpl` |
| 119 | **Glass Zone Bells** | `Keys` | Struck glass bell multisample with Hirajoshi scale | `119-Bells+Mallets-Glass Zone Bells.xpl` |
| 120 | **Kalimba Tines** | `Pluck` | African wooden thumb piano struck metal tines | `120-Bells+Mallets-Kalimba Tines.xpl` |
| 121 | **Marimba Layer Keys** | `Keys` | Multisampled marimba layers | `121-Bells+Mallets-Marimba Layer Keys.xpl` |
| 122 | **Marimba Synth** | `Pluck` | Warm struck wooden marimba bar resonance | `122-Bells+Mallets-Marimba Synth.xpl` |
| 123 | **Ring Mod Gamelan** | `Pluck` | Inharmonic metallic gamelan bar strike with ring modulation | `123-Bells+Mallets-Ring Mod Gamelan.xpl` |
| 124 | **Vibraphone Bars** | `Keys` | Warm jazz vibraphone struck metal bars with tremolo | `124-Bells+Mallets-Vibraphone Bars.xpl` |
| 125 | **Warm Toy Piano** | `Keys` | Lo-fi indie toy piano with struck bell-like tines | `125-Bells+Mallets-Warm Toy Piano.xpl` |

### 12. Strings+Brass (8 presets)

*Orchestral string ensembles, brass sections, accordions, and baroque harpsichords*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 126 | **Accordion Reed** | `Keys` | Dual reed acoustic folk accordion emulation | `126-Strings+Brass-Accordion Reed.xpl` |
| 127 | **Bowed MPE Strings** | `Pad` | Expressive bowed string ensemble with pad pressure vibrato | `127-Strings+Brass-Bowed MPE Strings.xpl` |
| 128 | **Bowed Pressure Strings** | `Pad` | Expressive bowed string ensemble with pad pressure vibrato | `128-Strings+Brass-Bowed Pressure Strings.xpl` |
| 129 | **Brass Swell Ensemble** | `Pad` | 3-oscillator swelling brass ensemble with filter dynamics | `129-Strings+Brass-Brass Swell Ensemble.xpl` |
| 130 | **Harpsichord Bar** | `Keys` | Crisp baroque plucked harpsichord strings | `130-Strings+Brass-Harpsichord Bar.xpl` |
| 131 | **Pizzicato Strings** | `Pluck` | Orchestral string section short plucked pizzicato | `131-Strings+Brass-Pizzicato Strings.xpl` |
| 132 | **Velvet Strings** | `Pad` | Silky smooth orchestral synth string section | `132-Strings+Brass-Velvet Strings.xpl` |
| 133 | **Vintage Brass Pad** | `Pad` | Classic 80s warm synth brass ensemble pad | `133-Strings+Brass-Vintage Brass Pad.xpl` |

### 13. Sequence (5 presets)

*Self-playing 16-step pentatonic grooves, tempo-synced chord gates, and fast arps*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 134 | **Fast Arp Bass** | `Bass` | Fast sequenced trance arpeggio bassline | `134-Sequence-Fast Arp Bass.xpl` |
| 135 | **Pentatonic Step Pluck** | `Pluck` | Self-playing 16-step pentatonic arpeggio sequence with synced LFO | `135-Sequence-Pentatonic Step Pluck.xpl` |
| 136 | **Radiant Pulse Pad** | `Pad` | Pulsing rhythmic pad with tempo-synced chord pulse | `136-Sequence-Radiant Pulse Pad.xpl` |
| 137 | **Tempo Gate Chords** | `Keys` | Chords rhythmic chopped by a 16-step tempo-synced gate LFO | `137-Sequence-Tempo Gate Chords.xpl` |
| 138 | **Trance Short Pluck** | `Pluck` | Punchy sequenced trance groove pluck with tight decay | `138-Sequence-Trance Short Pluck.xpl` |

### 14. Texture (8 presets)

*Continuous atmospheric noise beds, vinyl crackle, drones, and ambient windscapes*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 139 | **Alien Communication** | `FX` | Resonant formant alien vocal chatter soundscape | `139-Texture-Alien Communication.xpl` |
| 140 | **Bode Shifter Drone** | `FX` | Slow metallic feedback drone through Bode frequency shifter | `140-Texture-Bode Shifter Drone.xpl` |
| 141 | **Breath of Machines** | `FX` | Breathing industrial atmosphere with formant noise and Lorenz LFO | `141-Texture-Breath of Machines.xpl` |
| 142 | **Dark Cinema Drone** | `Pad` | Low suspenseful cinematic atmospheric drone | `142-Texture-Dark Cinema Drone.xpl` |
| 143 | **Shepard Ascent** | `Pad` | Endless ascending Shepard tone drone with Lorenz LFO modulation | `143-Texture-Shepard Ascent.xpl` |
| 144 | **Space Wind Storm** | `FX` | Howling ambient cosmic wind noise soundscape | `144-Texture-Space Wind Storm.xpl` |
| 145 | **Tri-Band Split Texture** | `Pad` | Multiband three-way split texture with distortion, utility, and chorus | `145-Texture-Tri-Band Split Texture.xpl` |
| 146 | **Vinyl Dust Crackle** | `FX` | Continuous vintage vinyl record dust and crackle generator | `146-Texture-Vinyl Dust Crackle.xpl` |

### 15. FX (13 presets)

*Transitional risers, pitch sweeps, teleport zaps, impacts, and one-shot effects*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 147 | **Bode Barber Riser** | `FX` | Rising saw and noise build with recursive Bode frequency shifter | `147-FX-Bode Barber Riser.xpl` |
| 148 | **Cosmic Teleport** | `FX` | Ascending pitch sweep dimensional teleport effect | `148-FX-Cosmic Teleport.xpl` |
| 149 | **Detonation Tail** | `FX` | Cinematic impact hit with sub drop and reverberant tail | `149-FX-Detonation Tail.xpl` |
| 150 | **Game Over Zap** | `FX` | Retro 8-bit arcade explosion zap one-shot | `150-FX-Game Over Zap.xpl` |
| 151 | **Gong Grain Swarm** | `FX` | Inharmonic metallic grain swarm effect | `151-FX-Gong Grain Swarm.xpl` |
| 152 | **Metal Gong Strike** | `FX` | Physical resonant metallic gong strike percussion | `152-FX-Metal Gong Strike.xpl` |
| 153 | **Noise Riser** | `FX` | Filtered white noise transitional build-up riser | `153-FX-Noise Riser.xpl` |
| 154 | **Reverse Gong Swell** | `FX` | Reversed gong swell effect | `154-FX-Reverse Gong Swell.xpl` |
| 155 | **Rising Grain Riser** | `FX` | Rising granular riser effect | `155-FX-Rising Grain Riser.xpl` |
| 156 | **Sci-Fi Laser Sweep** | `FX` | Downward pitch-modulated sci-fi laser burst | `156-FX-Sci-Fi Laser Sweep.xpl` |
| 157 | **Sonar Ping** | `FX` | Deep ocean submarine acoustic sonar ping | `157-FX-Sonar Ping.xpl` |
| 158 | **Spectral Chord Morph** | `FX` | Spectral chord morphing effect | `158-FX-Spectral Chord Morph.xpl` |
| 159 | **Sub Drop Boom** | `FX` | Subwoofer-testing downward pitch drop boom impact | `159-FX-Sub Drop Boom.xpl` |

---

## Alphabetical Preset Index

| Preset Name | Category Group | # | Origin Folder |
|---|---|---|---|
| **808 Boom** | Bass Sub+808 | 000 | `Bass` |
| **Accordion Reed** | Strings+Brass | 126 | `Keys` |
| **Acid Saw Reso** | Bass Acid | 012 | `Bass` |
| **Acoustic Style Pluck** | Pluck | 100 | `Pluck` |
| **Alien Communication** | Texture | 139 | `FX` |
| **Aux Keyscale Pluck** | Pluck | 101 | `Pluck` |
| **B3 Drawbar Organ** | Keys | 083 | `Keys` |
| **Bell Chime Lead** | Bells+Mallets | 115 | `Lead` |
| **Bell FM Ripple Lead** | Lead Sync+FM | 050 | `Lead` |
| **Bell Mallet Pluck** | Bells+Mallets | 116 | `Pluck` |
| **Bend Formant Growl** | Bass Acid | 013 | `Bass` |
| **Bode Barber Riser** | FX | 147 | `FX` |
| **Bode Shifter Drone** | Texture | 140 | `FX` |
| **Bowed MPE Strings** | Strings+Brass | 127 | `Pad` |
| **Bowed Pressure Strings** | Strings+Brass | 128 | `Pad` |
| **Brass Swell Ensemble** | Strings+Brass | 129 | `Pad` |
| **Breakbeat Slice Keys** | Keys | 084 | `Keys` |
| **Breath Flute Lead** | Lead Synth | 028 | `Lead` |
| **Breath of Machines** | Texture | 141 | `FX` |
| **Cathedral Choir** | Pad Warm | 056 | `Pad` |
| **Celesta Bells** | Bells+Mallets | 117 | `Keys` |
| **Chime Drop** | Bells+Mallets | 118 | `Pluck` |
| **Chord Cloud Pad** | Pad Evolving | 066 | `Pad` |
| **Chorused Saw Solo** | Lead Synth | 029 | `Lead` |
| **Clavinet Funk** | Keys | 085 | `Keys` |
| **Click Attack Bass** | Bass Synth | 018 | `Bass` |
| **Comb Metal Bass** | Bass Synth | 019 | `Bass` |
| **Comb Reso Swell** | Pad Evolving | 067 | `Pad` |
| **Comb Zither** | Pluck | 102 | `Pluck` |
| **Cosmic Drift Lead** | Lead Synth | 030 | `Lead` |
| **Cosmic Teleport** | FX | 148 | `FX` |
| **Custom Shape Wobble** | Bass Reese | 006 | `Bass` |
| **Dark Cinema Drone** | Texture | 142 | `Pad` |
| **Deep Sea Ambient** | Pad Evolving | 068 | `Pad` |
| **Detonation Tail** | FX | 149 | `FX` |
| **Diffusor Cloud Pad** | Pad Evolving | 069 | `Pad` |
| **Digital Glass Tap** | Pluck | 103 | `Pluck` |
| **Diode Bite Lead** | Lead Synth | 031 | `Lead` |
| **Diode Saturated Bass** | Bass Acid | 014 | `Bass` |
| **Dirty Distortion Bass** | Bass Acid | 015 | `Bass` |
| **Dirty Peak Lead** | Lead Synth | 032 | `Lead` |
| **Drawbar Organ Keys** | Keys | 086 | `Keys` |
| **Dream Grand Piano** | Keys | 087 | `Keys` |
| **Dual Octave Hero** | Lead Synth | 033 | `Lead` |
| **Dusty Rhodes Keys** | Keys | 088 | `Keys` |
| **Dyno Electric Piano** | Keys | 089 | `Keys` |
| **Electric Mute Pluck** | Pluck | 104 | `Pluck` |
| **EMS Squelch Bass** | Bass Acid | 016 | `Bass` |
| **Ethereal Glass** | Pad Evolving | 070 | `Pad` |
| **Fast Arp Bass** | Sequence | 134 | `Bass` |
| **Fat Reese** | Bass Reese | 007 | `Bass` |
| **FM Digital Lead** | Lead Sync+FM | 051 | `Lead` |
| **FM Soft Organ** | Keys | 090 | `Keys` |
| **Formant Pop Pluck** | Pluck | 105 | `Pluck` |
| **Formant Vowel Bass** | Bass Synth | 020 | `Bass` |
| **Frozen Aurora** | Pad Evolving | 071 | `Pad` |
| **Frozen Pluck Drone** | Pad Evolving | 072 | `Pad` |
| **Game Over Zap** | FX | 150 | `FX` |
| **Gentle Flute Pad** | Pad Warm | 057 | `Pad` |
| **Glass Organ Tones** | Keys | 091 | `Keys` |
| **Glass Ping Pluck** | Pluck | 106 | `Pluck` |
| **Glass Zone Bells** | Bells+Mallets | 119 | `Keys` |
| **Golden Dawn Pad** | Pad Warm | 058 | `Pad` |
| **Gong Grain Swarm** | FX | 151 | `FX` |
| **Gritty Square Bass** | Bass Synth | 021 | `Bass` |
| **Harmonic Cloud** | Pad Evolving | 073 | `Pad` |
| **Harmonic Shimmer Lead** | Lead Synth | 034 | `Lead` |
| **Harmonic Sine Bass** | Bass Sub+808 | 001 | `Bass` |
| **Harmonic Tine Pluck** | Pluck | 107 | `Pluck` |
| **Harp Drops** | Pluck | 108 | `Pluck` |
| **Harpsichord Bar** | Strings+Brass | 130 | `Keys` |
| **Hyper Dimension Pad** | Pad Warm | 059 | `Pad` |
| **Hyper Saw Lead** | Lead Synth | 035 | `Lead` |
| **Infinite Horizon** | Pad Evolving | 074 | `Pad` |
| **Just Intonation Keys** | Keys | 092 | `Keys` |
| **Kalimba Tines** | Bells+Mallets | 120 | `Pluck` |
| **Karplus Harp** | Pluck | 109 | `Pluck` |
| **Ladder FM Bass** | Bass Synth | 022 | `Bass` |
| **Laser Beam Lead** | Lead Sync+FM | 052 | `Lead` |
| **Lo-Fi Rhodes** | Keys | 093 | `Keys` |
| **Lorenz Garden** | Pad Evolving | 075 | `Pad` |
| **Lush Analog Pad** | Pad Warm | 060 | `Pad` |
| **Marimba Layer Keys** | Bells+Mallets | 121 | `Keys` |
| **Marimba Synth** | Bells+Mallets | 122 | `Pluck` |
| **Metal Gong Strike** | FX | 152 | `FX` |
| **Metallic Edge Lead** | Lead Synth | 036 | `Lead` |
| **Mid Side Halo** | Pad Warm | 061 | `Pad` |
| **Moog Ladder Bass** | Bass Synth | 023 | `Bass` |
| **MPE Pressure Lead** | Lead Synth | 037 | `Lead` |
| **Muted Honky Tonk** | Keys | 094 | `Keys` |
| **Noise Riser** | FX | 153 | `FX` |
| **Nylon Zone Pluck** | Pluck | 110 | `Pluck` |
| **Odd Harmonics Bass** | Bass Synth | 024 | `Bass` |
| **Odd Harmonics Pad** | Pad Warm | 062 | `Pad` |
| **Parallel Dusk Pad** | Pad Evolving | 076 | `Pad` |
| **Pentatonic Step Pluck** | Sequence | 135 | `Pluck` |
| **Phaser Sweep Lead** | Lead Synth | 038 | `Lead` |
| **Pitch Drop 808** | Bass Sub+808 | 002 | `Bass` |
| **Pizzicato Strings** | Strings+Brass | 131 | `Pluck` |
| **Plucked Lead Tone** | Lead Synth | 039 | `Lead` |
| **Pulse Width Bass** | Bass Synth | 025 | `Bass` |
| **Pulse Width Mod Lead** | Lead Synth | 040 | `Lead` |
| **Punchy Pluck Bass** | Bass Synth | 026 | `Bass` |
| **Radiant Pulse Pad** | Sequence | 136 | `Pad` |
| **Reese Dual Phase Bass** | Bass Reese | 008 | `Bass` |
| **Reverse Gong Swell** | FX | 154 | `FX` |
| **Ring Mod Gamelan** | Bells+Mallets | 123 | `Pluck` |
| **Rising Grain Riser** | FX | 155 | `FX` |
| **Rotary Rock Organ** | Keys | 095 | `Keys` |
| **Rubber Bass Pluck** | Pluck | 111 | `Pluck` |
| **Sci-Fi Laser Sweep** | FX | 156 | `FX` |
| **Scream Filter Bass** | Bass Reese | 009 | `Bass` |
| **Screaming Sync Lead** | Lead Sync+FM | 053 | `Lead` |
| **Shepard Ascent** | Texture | 143 | `Pad` |
| **Slow Motion Waves** | Pad Evolving | 077 | `Pad` |
| **Solid Sub** | Bass Sub+808 | 003 | `Bass` |
| **Solo Square Lead** | Lead Synth | 040 | `Lead` |
| **Sonar Ping** | FX | 157 | `FX` |
| **Space Odyssey Pad** | Pad Evolving | 078 | `Pad` |
| **Space Wind Storm** | Texture | 144 | `FX` |
| **Space Wobble Bass** | Bass Reese | 010 | `Bass` |
| **Spectral Bowed Glass** | Lead Synth | 041 | `Lead` |
| **Spectral Chord Morph** | FX | 158 | `FX` |
| **Spectral Gong Pad** | Pad Evolving | 079 | `Pad` |
| **Spectral Vowel Freeze** | Pad Evolving | 080 | `Pad` |
| **Spruce Guitar** | Pluck | 112 | `Pluck` |
| **Staccato Bleep Lead** | Lead Synth | 042 | `Lead` |
| **Starlight Shimmer** | Pad Warm | 063 | `Pad` |
| **Stretched Glass Choir** | Pad Evolving | 081 | `Pad` |
| **Stutter Grain Kit** | Pluck | 113 | `Pluck` |
| **Sub Drop Boom** | FX | 159 | `FX` |
| **Sub Duo Octave** | Bass Sub+808 | 004 | `Bass` |
| **Subterranean Duo** | Bass Sub+808 | 005 | `Bass` |
| **Subtle Tape Drift** | Pad Warm | 064 | `Pad` |
| **Sync Cry Lead** | Lead Sync+FM | 054 | `Lead` |
| **Sync Growl Bass** | Bass Acid | 017 | `Bass` |
| **Tempo Gate Chords** | Sequence | 137 | `Keys` |
| **Throat Singer Drone** | Pad Evolving | 082 | `Pad` |
| **Tine Glass Keys** | Keys | 096 | `Keys` |
| **Trance Short Pluck** | Sequence | 138 | `Pluck` |
| **Tri Soft Solo** | Lead Synth | 043 | `Lead` |
| **Tri-Band Split Texture** | Texture | 145 | `Pad` |
| **Triple Fifth Lead** | Lead Synth | 044 | `Lead` |
| **Triple Fold Reese** | Bass Reese | 011 | `Bass` |
| **Twin Warp Lead** | Lead Sync+FM | 055 | `Lead` |
| **Utility Width Keys** | Keys | 097 | `Keys` |
| **Velvet Strings** | Strings+Brass | 132 | `Pad` |
| **Vibraphone Bars** | Bells+Mallets | 124 | `Keys` |
| **Vintage Brass Pad** | Strings+Brass | 133 | `Pad` |
| **Vintage Electric Tine** | Keys | 098 | `Keys` |
| **Vinyl Dust Crackle** | Texture | 146 | `FX` |
| **Vocaloid Lead** | Lead Synth | 045 | `Lead` |
| **Vowel Choir Pad** | Pad Warm | 065 | `Pad` |
| **Vowel Scrub Choir** | Lead Synth | 046 | `Lead` |
| **Vowel Table Lead** | Lead Synth | 047 | `Lead` |
| **Vowel Whistle Lead** | Lead Synth | 048 | `Lead` |
| **Wah Pressure Clav** | Keys | 099 | `Keys` |
| **Warm Analog Bass** | Bass Synth | 027 | `Bass` |
| **Warm Toy Piano** | Bells+Mallets | 125 | `Keys` |
| **Warm Vintage Lead** | Lead Synth | 049 | `Lead` |
