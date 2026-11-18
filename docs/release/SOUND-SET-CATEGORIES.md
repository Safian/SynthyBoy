# Sound Set Browser Categories: Review & Mapping

Status: **Proposed for review by the owner.**

This document details the browser category organization for SynthyBoy factory presets in the Akai MPC host Sounds browser. Presets are grouped in a musician-friendly manner into 15 focused sub-styles (maximum 16 groups, all groups contain at least 4 presets).

## Browser Rules & Stock Conventions

- **Category Name Characters**: Stock Akai MPC instruments avoid slashes (`/`) and punctuation in category names, using letters, spaces, digits, and `+` (e.g. `Synth Bass`, `Plucks+Synths`, `Pitched Perc+Bells`). Categories here follow this rule exactly (e.g. `Bass Sub+808`, `Lead Sync+FM`, `Bells+Mallets`, `Strings+Brass`).
- **File Naming & Numbering**: `scripts/gen-xpl-presets.py` names files as `NNN-<Category>-<Preset Name>.xpl`. Files are numbered sequentially (`000` .. `183`) such that presets within each group stay strictly contiguous in the group list order, and presets within each group sort alphabetically.
- **Integrity & Extension**: `scripts/assign-preset-categories.py --check` enforces that all presets exist, all groups meet the 4-preset minimum, and all names are FAT/stock-compliant. `--suggest` provides deterministic recommendations for future sound set additions.

## Group Summary

| # | Group Name | Presets | Origin Folders | Musical Style / Sound Character |
|---|---|---|---|---|
| 01 | **Bass Sub+808** | 7 | Bass | Deep pure sine sub-bass, 808 pitch-drop booms, and low-end foundations |
| 02 | **Bass Reese** | 6 | Bass | Detuned multi-voice saw basslines, wavefolded reeses, and rhythmic wobbles |
| 03 | **Bass Acid** | 7 | Bass | 303-style resonant squelches, diode saturation, distortion, and aggressive growls |
| 04 | **Bass Synth** | 13 | Bass | General analog, punchy transient, hollow funk, and pulse-width basslines |
| 05 | **Lead Synth** | 28 | Lead | Solo anthems, supersaws, expressive analog, and vocal formant leads |
| 06 | **Lead Sync+FM** | 8 | Lead | Hard-sync pitched oscillators, frequency-modulated digital leads, and warp cutters |
| 07 | **Pad Warm** | 12 | Pad | Classic analog strings, warm choirs, vintage brass swells, and lush chord beds |
| 08 | **Pad Evolving** | 19 | Pad | Slow morphing wavetables, ambient diffusor washes, and shifting spectral clouds |
| 09 | **Keys** | 18 | Keys | Acoustic grands, Rhodes/dyno electric pianos, drawbar/rock organs, and clavs |
| 10 | **Pluck** | 14 | Pluck | Acoustic guitars, Karplus-Strong physical strings, zithers, and transient plucks |
| 11 | **Bells+Mallets** | 14 | Keys, Lead, Pluck | Tuned percussion, celestas, vibraphones, marimbas, kalimbas, and struck gamelans |
| 12 | **Strings+Brass** | 8 | Keys, Pad, Pluck | Orchestral string ensembles, brass sections, accordions, and baroque harpsichords |
| 13 | **Sequence** | 6 | Bass, Keys, Pad, Pluck | Self-playing 16-step pentatonic grooves, tempo-synced chord gates, and fast arps |
| 14 | **Texture** | 9 | FX, Pad | Continuous atmospheric noise beds, vinyl crackle, drones, and ambient windscapes |
| 15 | **FX** | 15 | FX | Transitional risers, pitch sweeps, teleport zaps, impacts, and one-shot effects |

---

## Presets by Group

### 1. Bass Sub+808 (7 presets)

*Deep pure sine sub-bass, 808 pitch-drop booms, and low-end foundations*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 000 | **808 Boom** | `Bass` | Pitch-gliding sine sub bass with classic 808 decay | `000-Bass Sub+808-808 Boom.xpl` |
| 001 | **Harmonic Sine Bass** | `Bass` | Smooth sine sub bass with subtle upper harmonics | `001-Bass Sub+808-Harmonic Sine Bass.xpl` |
| 002 | **Pink Noise Sub** | `Bass` | Sine sub bass with a closing pink-noise table thump | `002-Bass Sub+808-Pink Noise Sub.xpl` |
| 003 | **Pitch Drop 808** | `Bass` | Sub 808 with rapid envelope pitch drop and pulse knock | `003-Bass Sub+808-Pitch Drop 808.xpl` |
| 004 | **Solid Sub** | `Bass` | Deep pure sine sub bass foundation | `004-Bass Sub+808-Solid Sub.xpl` |
| 005 | **Sub Duo Octave** | `Bass` | Dual octave stacked pure sine sub bass | `005-Bass Sub+808-Sub Duo Octave.xpl` |
| 006 | **Subterranean Duo** | `Bass` | Pure sine sub paired with folded saw growl layer | `006-Bass Sub+808-Subterranean Duo.xpl` |

### 2. Bass Reese (6 presets)

*Detuned multi-voice saw basslines, wavefolded reeses, and rhythmic wobbles*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 007 | **Custom Shape Wobble** | `Bass` | Detuned dual-saw bass with rhythmic hand-drawn wobble LFO | `007-Bass Reese-Custom Shape Wobble.xpl` |
| 008 | **Fat Reese** | `Bass` | 5-voice detuned sawtooth Reese bass for drum and bass | `008-Bass Reese-Fat Reese.xpl` |
| 009 | **Reese Dual Phase Bass** | `Bass` | Dual-phase reese bass using wavetables | `009-Bass Reese-Reese Dual Phase Bass.xpl` |
| 010 | **Scream Filter Bass** | `Bass` | 3-voice detuned saw bass driven through scream filter | `010-Bass Reese-Scream Filter Bass.xpl` |
| 011 | **Space Wobble Bass** | `Bass` | Dub-style wobble bass with LFO-modulated lowpass cutoff | `011-Bass Reese-Space Wobble Bass.xpl` |
| 012 | **Triple Fold Reese** | `Bass` | 3-oscillator detuned saw reese with sine-fold wavefolding | `012-Bass Reese-Triple Fold Reese.xpl` |

### 3. Bass Acid (7 presets)

*303-style resonant squelches, diode saturation, distortion, and aggressive growls*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 013 | **Acid Saw Reso** | `Bass` | Classic 303-style resonant sawtooth acid line | `013-Bass Acid-Acid Saw Reso.xpl` |
| 014 | **Bend Formant Growl** | `Bass` | Aggressive bent saw growl through parallel formant and analog filters | `014-Bass Acid-Bend Formant Growl.xpl` |
| 015 | **Diode Saturated Bass** | `Bass` | Warm saturated diode ladder filter resonant bass | `015-Bass Acid-Diode Saturated Bass.xpl` |
| 016 | **Dirty Distortion Bass** | `Bass` | Overdriven industrial bass using dirty filter model | `016-Bass Acid-Dirty Distortion Bass.xpl` |
| 017 | **EMS Squelch Bass** | `Bass` | Resonant EMS ladder bass with short snappy filter envelope | `017-Bass Acid-EMS Squelch Bass.xpl` |
| 018 | **PD Reso Acid Bass** | `Bass` | Phase-distortion resonance table acid bass with diode bite | `018-Bass Acid-PD Reso Acid Bass.xpl` |
| 019 | **Sync Growl Bass** | `Bass` | Hard-sync modulated aggressive growl bass | `019-Bass Acid-Sync Growl Bass.xpl` |

### 4. Bass Synth (13 presets)

*General analog, punchy transient, hollow funk, and pulse-width basslines*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 020 | **Analog Sweep Bass** | `Bass` | Saw filter-sweep table bass with envelope bark and sine sub | `020-Bass Synth-Analog Sweep Bass.xpl` |
| 021 | **Click Attack Bass** | `Bass` | Punchy synth bass with transient click attack | `021-Bass Synth-Click Attack Bass.xpl` |
| 022 | **Comb Metal Bass** | `Bass` | Physical resonant comb filter metallic string bass | `022-Bass Synth-Comb Metal Bass.xpl` |
| 023 | **Formant Vowel Bass** | `Bass` | Formant filter sweeping vocal vowel bassline | `023-Bass Synth-Formant Vowel Bass.xpl` |
| 024 | **Gritty Square Bass** | `Bass` | Raw edgy lo-fi square wave synth bass | `024-Bass Synth-Gritty Square Bass.xpl` |
| 025 | **Harmonic Wah Bass** | `Bass` | Harmonic band-sweep table talking funk wah bass | `025-Bass Synth-Harmonic Wah Bass.xpl` |
| 026 | **Ladder FM Bass** | `Bass` | FM bass with ladder filter | `026-Bass Synth-Ladder FM Bass.xpl` |
| 027 | **Moog Ladder Bass** | `Bass` | Vintage analog ladder filter round bass | `027-Bass Synth-Moog Ladder Bass.xpl` |
| 028 | **Odd Harmonics Bass** | `Bass` | Hollow funk bass driven by odd harmonic series | `028-Bass Synth-Odd Harmonics Bass.xpl` |
| 029 | **Pulse Width Bass** | `Bass` | PWM modulated pulse wave electro bass | `029-Bass Synth-Pulse Width Bass.xpl` |
| 030 | **Punchy Pluck Bass** | `Bass` | Tight transient bass with short percussive decay | `030-Bass Synth-Punchy Pluck Bass.xpl` |
| 031 | **Sync Saw Raw Bass** | `Bass` | Hard-sync saw table raw mono bass with hard clip | `031-Bass Synth-Sync Saw Raw Bass.xpl` |
| 032 | **Warm Analog Bass** | `Bass` | Classic deep house warm analog lowpass bass | `032-Bass Synth-Warm Analog Bass.xpl` |

### 5. Lead Synth (28 presets)

*Solo anthems, supersaws, expressive analog, and vocal formant leads*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 033 | **Acoustic Bore Reed** | `Lead` | Clarinet to saxophone bore table reed lead morphed by pressure | `033-Lead Synth-Acoustic Bore Reed.xpl` |
| 034 | **Air Chiff Flute** | `Lead` | Breathy mono flute with noise-table chiff and pressure breath | `034-Lead Synth-Air Chiff Flute.xpl` |
| 035 | **Breath Flute Lead** | `Lead` | Breath-based flute lead using sample oscillator | `035-Lead Synth-Breath Flute Lead.xpl` |
| 036 | **Chorused Saw Solo** | `Lead` | Bright sawtooth solo lead bathed in stereo chorus | `036-Lead Synth-Chorused Saw Solo.xpl` |
| 037 | **Cosmic Drift Lead** | `Lead` | Slowly drifting ambient analog solo synth lead | `037-Lead Synth-Cosmic Drift Lead.xpl` |
| 038 | **Diode Bite Lead** | `Lead` | Sharp biting diode ladder filtered aggressive lead | `038-Lead Synth-Diode Bite Lead.xpl` |
| 039 | **Diphthong Morph Vox** | `Lead` | Eight-diphthong table talking lead with stepped LFO words | `039-Lead Synth-Diphthong Morph Vox.xpl` |
| 040 | **Dirty Peak Lead** | `Lead` | Distorted resonant peak filter industrial synth lead | `040-Lead Synth-Dirty Peak Lead.xpl` |
| 041 | **Dual Octave Hero** | `Lead` | Heroic stacked octave dual sawtooth anthem lead | `041-Lead Synth-Dual Octave Hero.xpl` |
| 042 | **Harmonic Shimmer Lead** | `Lead` | Upper harmonic rich shimmering solo synth lead | `042-Lead Synth-Harmonic Shimmer Lead.xpl` |
| 043 | **Hyper Saw Lead** | `Lead` | 7-voice detuned supersaw stadium anthem lead | `043-Lead Synth-Hyper Saw Lead.xpl` |
| 044 | **Metallic Edge Lead** | `Lead` | Aggressive electro lead with metallic overtone edge | `044-Lead Synth-Metallic Edge Lead.xpl` |
| 045 | **MPE Pressure Lead** | `Lead` | Expressive MPE solo lead with per-note pressure filter modulation | `045-Lead Synth-MPE Pressure Lead.xpl` |
| 046 | **Phaser Sweep Lead** | `Lead` | Spacey multi-stage phaser swept solo synth lead | `046-Lead Synth-Phaser Sweep Lead.xpl` |
| 047 | **Plucked Lead Tone** | `Lead` | Hybrid fast pluck transient with sustained body | `047-Lead Synth-Plucked Lead Tone.xpl` |
| 048 | **Pulse Width Mod Lead** | `Lead` | Classic 80s chorus-widened pulse-width modulated lead | `048-Lead Synth-Pulse Width Mod Lead.xpl` |
| 049 | **Solo Square Lead** | `Lead` | Expressive monophonic retro square wave solo lead | `049-Lead Synth-Solo Square Lead.xpl` |
| 050 | **Spectral Bowed Glass** | `Lead` | Spectral bowed glass lead | `050-Lead Synth-Spectral Bowed Glass.xpl` |
| 051 | **Staccato Bleep Lead** | `Lead` | Retro arcade-inspired punchy staccato bleep melody lead | `051-Lead Synth-Staccato Bleep Lead.xpl` |
| 052 | **Talk Box Baritone** | `Lead` | Men's vowel-cycle table talk-box lead with wheel mouth | `052-Lead Synth-Talk Box Baritone.xpl` |
| 053 | **Trance Stack Lead** | `Lead` | Seven-saw phase-spread table supersaw lead with fifth layer | `053-Lead Synth-Trance Stack Lead.xpl` |
| 054 | **Tri Soft Solo** | `Lead` | Flute-like soft triangle wave solo lead | `054-Lead Synth-Tri Soft Solo.xpl` |
| 055 | **Triple Fifth Lead** | `Lead` | Stacked 3-oscillator power-chord lead with fifth and octave layers | `055-Lead Synth-Triple Fifth Lead.xpl` |
| 056 | **Vocaloid Lead** | `Lead` | Singing formant vocal tract lead synthesizer | `056-Lead Synth-Vocaloid Lead.xpl` |
| 057 | **Vowel Scrub Choir** | `Lead` | Formant-scrubbing choir lead | `057-Lead Synth-Vowel Scrub Choir.xpl` |
| 058 | **Vowel Table Lead** | `Lead` | Vocal vowel lead using wavetables | `058-Lead Synth-Vowel Table Lead.xpl` |
| 059 | **Vowel Whistle Lead** | `Lead` | Mono legato vocal formant morph lead with expressive vibrato | `059-Lead Synth-Vowel Whistle Lead.xpl` |
| 060 | **Warm Vintage Lead** | `Lead` | Smooth vintage analog lowpass filtered solo lead | `060-Lead Synth-Warm Vintage Lead.xpl` |

### 6. Lead Sync+FM (8 presets)

*Hard-sync pitched oscillators, frequency-modulated digital leads, and warp cutters*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 061 | **Bell FM Ripple Lead** | `Lead` | Sine carrier phase-modulated FM bell lead with velocity bite | `061-Lead Sync+FM-Bell FM Ripple Lead.xpl` |
| 062 | **FM Digital Lead** | `Lead` | Crisp digital FM synthesis lead with sharp modulation | `062-Lead Sync+FM-FM Digital Lead.xpl` |
| 063 | **Laser Beam Lead** | `Lead` | Futuristic cutting sync and warp modulation laser lead | `063-Lead Sync+FM-Laser Beam Lead.xpl` |
| 064 | **Screaming Sync Lead** | `Lead` | Aggressive pitched oscillator hard-sync rock lead | `064-Lead Sync+FM-Screaming Sync Lead.xpl` |
| 065 | **Sync Cry Lead** | `Lead` | Expressive singing hard-sync saw lead with envelope sweep | `065-Lead Sync+FM-Sync Cry Lead.xpl` |
| 066 | **Sync Sine Screamer** | `Lead` | Hard-sync sine table mono lead with envelope scream | `066-Lead Sync+FM-Sync Sine Screamer.xpl` |
| 067 | **Twin Warp Lead** | `Lead` | Dual sync and bend warp stages in series with LFO sweep | `067-Lead Sync+FM-Twin Warp Lead.xpl` |
| 068 | **West Coast Wavefold** | `Lead` | Sine wave-folder table mono lead with wheel and pressure fold | `068-Lead Sync+FM-West Coast Wavefold.xpl` |

### 7. Pad Warm (12 presets)

*Classic analog strings, warm choirs, vintage brass swells, and lush chord beds*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 069 | **Cathedral Choir** | `Pad` | Vast reverberant sacred choir pad | `069-Pad Warm-Cathedral Choir.xpl` |
| 070 | **Choir Swell Ensemble** | `Pad` | Men, women and children choir table pad with long swell | `070-Pad Warm-Choir Swell Ensemble.xpl` |
| 071 | **Gentle Flute Pad** | `Pad` | Soft breathy woodwind flute ensemble pad | `071-Pad Warm-Gentle Flute Pad.xpl` |
| 072 | **Golden Dawn Pad** | `Pad` | Peaceful warm morning dawn analog pad | `072-Pad Warm-Golden Dawn Pad.xpl` |
| 073 | **Hyper Dimension Pad** | `Pad` | Sine-to-saw pad expanded with Hyper/Dimension width | `073-Pad Warm-Hyper Dimension Pad.xpl` |
| 074 | **Lush Analog Pad** | `Pad` | Warm enveloping classic analog string/synth pad | `074-Pad Warm-Lush Analog Pad.xpl` |
| 075 | **Mid Side Halo** | `Pad` | 3-oscillator warm pad with mid/side processing and wide halo | `075-Pad Warm-Mid Side Halo.xpl` |
| 076 | **Odd Harmonics Pad** | `Pad` | Warm hollow wooden pipe organ pad with odd harmonics | `076-Pad Warm-Odd Harmonics Pad.xpl` |
| 077 | **Soprano Vowel Pad** | `Pad` | Women's vowel-cycle table choir pad with slow vowel walk | `077-Pad Warm-Soprano Vowel Pad.xpl` |
| 078 | **Starlight Shimmer** | `Pad` | Bright crystalline shimmering high-register synth pad | `078-Pad Warm-Starlight Shimmer.xpl` |
| 079 | **Subtle Tape Drift** | `Pad` | Vintage warm pad with subtle tape flutter and pitch drift | `079-Pad Warm-Subtle Tape Drift.xpl` |
| 080 | **Vowel Choir Pad** | `Pad` | Synthetic choral pad with warm formant vowel blending | `080-Pad Warm-Vowel Choir Pad.xpl` |

### 8. Pad Evolving (19 presets)

*Slow morphing wavetables, ambient diffusor washes, and shifting spectral clouds*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 081 | **Chord Cloud Pad** | `Pad` | Granular cloud pad with four-chord progression drift | `081-Pad Evolving-Chord Cloud Pad.xpl` |
| 082 | **Comb Reso Swell** | `Pad` | Acoustic-like resonant comb filter swelling wash | `082-Pad Evolving-Comb Reso Swell.xpl` |
| 083 | **Deep Sea Ambient** | `Pad` | Submerged oceanic atmosphere with slow filter movement | `083-Pad Evolving-Deep Sea Ambient.xpl` |
| 084 | **Diffusor Cloud Pad** | `Pad` | Detuned saw pad smeared by Diffusor filter into slow drift | `084-Pad Evolving-Diffusor Cloud Pad.xpl` |
| 085 | **Dispersion Phase Pad** | `Pad` | Phase-dispersion table pad with soft clip and slow swirl | `085-Pad Evolving-Dispersion Phase Pad.xpl` |
| 086 | **Ethereal Glass** | `Pad` | Airy crystalline glass texture with slowly shifting overtones | `086-Pad Evolving-Ethereal Glass.xpl` |
| 087 | **Frozen Aurora** | `Pad` | Cold crystalline northern lights atmospheric soundscape | `087-Pad Evolving-Frozen Aurora.xpl` |
| 088 | **Frozen Pluck Drone** | `Pad` | Frozen granular pluck drone | `088-Pad Evolving-Frozen Pluck Drone.xpl` |
| 089 | **Gong Bloom Pad** | `Pad` | Gong-mode table pad that blooms with the swell envelope | `089-Pad Evolving-Gong Bloom Pad.xpl` |
| 090 | **Harmonic Cloud** | `Pad` | Dense evolving harmonic cluster pad with subtle modulation | `090-Pad Evolving-Harmonic Cloud.xpl` |
| 091 | **Infinite Horizon** | `Pad` | Massive cinematic landscape pad with endless evolving decay | `091-Pad Evolving-Infinite Horizon.xpl` |
| 092 | **Lorenz Garden** | `Pad` | Generative bell pad with chaotic Lorenz and Rossler LFO drift | `092-Pad Evolving-Lorenz Garden.xpl` |
| 093 | **Parallel Dusk Pad** | `Pad` | Dual parallel stereo filters with opposing LFO cutoff motion | `093-Pad Evolving-Parallel Dusk Pad.xpl` |
| 094 | **Slow Motion Waves** | `Pad` | Slow morphing wavetable sweep across spectral frames | `094-Pad Evolving-Slow Motion Waves.xpl` |
| 095 | **Space Odyssey Pad** | `Pad` | Expansive sci-fi atmospheric pad with cosmic movement | `095-Pad Evolving-Space Odyssey Pad.xpl` |
| 096 | **Spectral Gong Pad** | `Pad` | Spectral gong pad | `096-Pad Evolving-Spectral Gong Pad.xpl` |
| 097 | **Spectral Vowel Freeze** | `Pad` | Spectral vowel freezing pad | `097-Pad Evolving-Spectral Vowel Freeze.xpl` |
| 098 | **Stretched Glass Choir** | `Pad` | Spectral-stretched partials drifting across random LFOs | `098-Pad Evolving-Stretched Glass Choir.xpl` |
| 099 | **Throat Singer Drone** | `Pad` | Throat singer drone | `099-Pad Evolving-Throat Singer Drone.xpl` |

### 9. Keys (18 presets)

*Acoustic grands, Rhodes/dyno electric pianos, drawbar/rock organs, and clavs*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 100 | **B3 Drawbar Organ** | `Keys` | Warm tonewheel jazz drawbar organ | `100-Keys-B3 Drawbar Organ.xpl` |
| 101 | **Breakbeat Slice Keys** | `Keys` | Breakbeat-sliced sample keys with parallel crush | `101-Keys-Breakbeat Slice Keys.xpl` |
| 102 | **Clavinet Funk** | `Keys` | Funky 70s electronic clavinet keyboard | `102-Keys-Clavinet Funk.xpl` |
| 103 | **Drawbar Organ Keys** | `Keys` | Drawbar organ emulation using wavetable oscillator | `103-Keys-Drawbar Organ Keys.xpl` |
| 104 | **Dream Grand Piano** | `Keys` | Acoustic grand piano style dynamic keyboard | `104-Keys-Dream Grand Piano.xpl` |
| 105 | **Dusty Rhodes Keys** | `Keys` | Dusty keys emulation with sampled Rhodes | `105-Keys-Dusty Rhodes Keys.xpl` |
| 106 | **Dyno Electric Piano** | `Keys` | Bright modified 80s dyno tine electric piano | `106-Keys-Dyno Electric Piano.xpl` |
| 107 | **FM Chain Tine Keys** | `Keys` | Three-operator FM chain electric piano with tine decay | `107-Keys-FM Chain Tine Keys.xpl` |
| 108 | **FM Soft Organ** | `Keys` | Smooth gospel tonewheel organ using FM synthesis | `108-Keys-FM Soft Organ.xpl` |
| 109 | **Glass Organ Tones** | `Keys` | Crystalline cathedral glass church organ | `109-Keys-Glass Organ Tones.xpl` |
| 110 | **Just Intonation Keys** | `Keys` | Pure harmonic-series keyboard tuned in just intonation | `110-Keys-Just Intonation Keys.xpl` |
| 111 | **Lo-Fi Rhodes** | `Keys` | Mellow chillhop Rhodes electric piano with warm tape saturation | `111-Keys-Lo-Fi Rhodes.xpl` |
| 112 | **Muted Honky Tonk** | `Keys` | Saloon-style upright piano with detuned unison tines | `112-Keys-Muted Honky Tonk.xpl` |
| 113 | **Rotary Rock Organ** | `Keys` | Gritty rock organ through rotary Leslie speaker chorus | `113-Keys-Rotary Rock Organ.xpl` |
| 114 | **Tine Glass Keys** | `Keys` | Electric piano combining FM sine body with glassy bell tine ping | `114-Keys-Tine Glass Keys.xpl` |
| 115 | **Utility Width Keys** | `Keys` | Chorused triangle and square keys with mono low-end utility | `115-Keys-Utility Width Keys.xpl` |
| 116 | **Vintage Electric Tine** | `Keys` | Classic warm Rhodes electric piano with soft tines | `116-Keys-Vintage Electric Tine.xpl` |
| 117 | **Wah Pressure Clav** | `Keys` | Expressive funk clavinet with pad pressure sweeping wah filter | `117-Keys-Wah Pressure Clav.xpl` |

### 10. Pluck (14 presets)

*Acoustic guitars, Karplus-Strong physical strings, zithers, and transient plucks*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 118 | **Acoustic Style Pluck** | `Pluck` | Crisp acoustic guitar transient steel-string pluck | `118-Pluck-Acoustic Style Pluck.xpl` |
| 119 | **Aux Keyscale Pluck** | `Pluck` | Plucked synth with keyboard-scaled dynamic filter sweep | `119-Pluck-Aux Keyscale Pluck.xpl` |
| 120 | **Comb Zither** | `Pluck` | Physical modeled plucked zither string using comb resonance | `120-Pluck-Comb Zither.xpl` |
| 121 | **Digital Glass Tap** | `Pluck` | Modern UI glassy transient percussive tap tone | `121-Pluck-Digital Glass Tap.xpl` |
| 122 | **Electric Mute Pluck** | `Pluck` | Palm-muted electric guitar style synth pluck | `122-Pluck-Electric Mute Pluck.xpl` |
| 123 | **Formant Pop Pluck** | `Pluck` | Short staccato vocal formant pop pluck | `123-Pluck-Formant Pop Pluck.xpl` |
| 124 | **Glass Ping Pluck** | `Pluck` | Glassy plucked sound using sampled glass | `124-Pluck-Glass Ping Pluck.xpl` |
| 125 | **Harmonic Tine Pluck** | `Pluck` | Rich harmonic struck tine transient pluck | `125-Pluck-Harmonic Tine Pluck.xpl` |
| 126 | **Harp Drops** | `Pluck` | Fluid crystalline concert harp plucked string drops | `126-Pluck-Harp Drops.xpl` |
| 127 | **Karplus Harp** | `Pluck` | Karplus-Strong physical string model excited by noise burst | `127-Pluck-Karplus Harp.xpl` |
| 128 | **Nylon Zone Pluck** | `Pluck` | Nylon plucked multisample | `128-Pluck-Nylon Zone Pluck.xpl` |
| 129 | **Rubber Bass Pluck** | `Pluck` | Bouncy rubberized synth pluck with elastic filter response | `129-Pluck-Rubber Bass Pluck.xpl` |
| 130 | **Spruce Guitar** | `Pluck` | Clean acoustic guitar pluck with warm spruce resonance | `130-Pluck-Spruce Guitar.xpl` |
| 131 | **Stutter Grain Kit** | `Pluck` | Stutter grain drum kit | `131-Pluck-Stutter Grain Kit.xpl` |

### 11. Bells+Mallets (14 presets)

*Tuned percussion, celestas, vibraphones, marimbas, kalimbas, and struck gamelans*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 132 | **Bell Chime Lead** | `Lead` | Crystalline bell overtones and struck chime lead | `132-Bells+Mallets-Bell Chime Lead.xpl` |
| 133 | **Bell Mallet Pluck** | `Pluck` | Percussive bell mallet strike with metallic overtones | `133-Bells+Mallets-Bell Mallet Pluck.xpl` |
| 134 | **Cathedral Bell Ring** | `Keys` | Additive bronze bell strike table settling to the hum | `134-Bells+Mallets-Cathedral Bell Ring.xpl` |
| 135 | **Celesta Bells** | `Keys` | Fairytale orchestral celesta bell keyboard | `135-Bells+Mallets-Celesta Bells.xpl` |
| 136 | **Chime Drop** | `Pluck` | Delicate falling metallic glass chime cluster | `136-Bells+Mallets-Chime Drop.xpl` |
| 137 | **Free Bar Vibraphone** | `Keys` | Free-bar mode table vibraphone with wheel motor tremolo | `137-Bells+Mallets-Free Bar Vibraphone.xpl` |
| 138 | **Glass Zone Bells** | `Keys` | Struck glass bell multisample with Hirajoshi scale | `138-Bells+Mallets-Glass Zone Bells.xpl` |
| 139 | **Kalimba Tines** | `Pluck` | African wooden thumb piano struck metal tines | `139-Bells+Mallets-Kalimba Tines.xpl` |
| 140 | **Marimba Layer Keys** | `Keys` | Multisampled marimba layers | `140-Bells+Mallets-Marimba Layer Keys.xpl` |
| 141 | **Marimba Synth** | `Pluck` | Warm struck wooden marimba bar resonance | `141-Bells+Mallets-Marimba Synth.xpl` |
| 142 | **Ring Mod Gamelan** | `Pluck` | Inharmonic metallic gamelan bar strike with ring modulation | `142-Bells+Mallets-Ring Mod Gamelan.xpl` |
| 143 | **Shimmer Bell Walk** | `Keys` | Church bell table whose partials open with velocity | `143-Bells+Mallets-Shimmer Bell Walk.xpl` |
| 144 | **Vibraphone Bars** | `Keys` | Warm jazz vibraphone struck metal bars with tremolo | `144-Bells+Mallets-Vibraphone Bars.xpl` |
| 145 | **Warm Toy Piano** | `Keys` | Lo-fi indie toy piano with struck bell-like tines | `145-Bells+Mallets-Warm Toy Piano.xpl` |

### 12. Strings+Brass (8 presets)

*Orchestral string ensembles, brass sections, accordions, and baroque harpsichords*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 146 | **Accordion Reed** | `Keys` | Dual reed acoustic folk accordion emulation | `146-Strings+Brass-Accordion Reed.xpl` |
| 147 | **Bowed MPE Strings** | `Pad` | Expressive bowed string ensemble with pad pressure vibrato | `147-Strings+Brass-Bowed MPE Strings.xpl` |
| 148 | **Bowed Pressure Strings** | `Pad` | Expressive bowed string ensemble with pad pressure vibrato | `148-Strings+Brass-Bowed Pressure Strings.xpl` |
| 149 | **Brass Swell Ensemble** | `Pad` | 3-oscillator swelling brass ensemble with filter dynamics | `149-Strings+Brass-Brass Swell Ensemble.xpl` |
| 150 | **Harpsichord Bar** | `Keys` | Crisp baroque plucked harpsichord strings | `150-Strings+Brass-Harpsichord Bar.xpl` |
| 151 | **Pizzicato Strings** | `Pluck` | Orchestral string section short plucked pizzicato | `151-Strings+Brass-Pizzicato Strings.xpl` |
| 152 | **Velvet Strings** | `Pad` | Silky smooth orchestral synth string section | `152-Strings+Brass-Velvet Strings.xpl` |
| 153 | **Vintage Brass Pad** | `Pad` | Classic 80s warm synth brass ensemble pad | `153-Strings+Brass-Vintage Brass Pad.xpl` |

### 13. Sequence (6 presets)

*Self-playing 16-step pentatonic grooves, tempo-synced chord gates, and fast arps*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 154 | **Fast Arp Bass** | `Bass` | Fast sequenced trance arpeggio bassline | `154-Sequence-Fast Arp Bass.xpl` |
| 155 | **Pentatonic Step Pluck** | `Pluck` | Self-playing 16-step pentatonic arpeggio sequence with synced LFO | `155-Sequence-Pentatonic Step Pluck.xpl` |
| 156 | **Radiant Pulse Pad** | `Pad` | Pulsing rhythmic pad with tempo-synced chord pulse | `156-Sequence-Radiant Pulse Pad.xpl` |
| 157 | **Step Morph Pluck** | `Pluck` | Wave-sequencing step table pluck with stepped LFO and echoes | `157-Sequence-Step Morph Pluck.xpl` |
| 158 | **Tempo Gate Chords** | `Keys` | Chords rhythmic chopped by a 16-step tempo-synced gate LFO | `158-Sequence-Tempo Gate Chords.xpl` |
| 159 | **Trance Short Pluck** | `Pluck` | Punchy sequenced trance groove pluck with tight decay | `159-Sequence-Trance Short Pluck.xpl` |

### 14. Texture (9 presets)

*Continuous atmospheric noise beds, vinyl crackle, drones, and ambient windscapes*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 160 | **Alien Communication** | `FX` | Resonant formant alien vocal chatter soundscape | `160-Texture-Alien Communication.xpl` |
| 161 | **Bode Shifter Drone** | `FX` | Slow metallic feedback drone through Bode frequency shifter | `161-Texture-Bode Shifter Drone.xpl` |
| 162 | **Breath of Machines** | `FX` | Breathing industrial atmosphere with formant noise and Lorenz LFO | `162-Texture-Breath of Machines.xpl` |
| 163 | **Dark Cinema Drone** | `Pad` | Low suspenseful cinematic atmospheric drone | `163-Texture-Dark Cinema Drone.xpl` |
| 164 | **Shepard Ascent** | `Pad` | Endless ascending Shepard tone drone with Lorenz LFO modulation | `164-Texture-Shepard Ascent.xpl` |
| 165 | **Space Wind Storm** | `FX` | Howling ambient cosmic wind noise soundscape | `165-Texture-Space Wind Storm.xpl` |
| 166 | **Tri-Band Split Texture** | `Pad` | Multiband three-way split texture with distortion, utility, and chorus | `166-Texture-Tri-Band Split Texture.xpl` |
| 167 | **Vinyl Crackle Bed** | `FX` | Crackle-pulse table dust bed with vinyl ticks and basin reverb | `167-Texture-Vinyl Crackle Bed.xpl` |
| 168 | **Vinyl Dust Crackle** | `FX` | Continuous vintage vinyl record dust and crackle generator | `168-Texture-Vinyl Dust Crackle.xpl` |

### 15. FX (15 presets)

*Transitional risers, pitch sweeps, teleport zaps, impacts, and one-shot effects*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 169 | **Bode Barber Riser** | `FX` | Rising saw and noise build with recursive Bode frequency shifter | `169-FX-Bode Barber Riser.xpl` |
| 170 | **Cosmic Teleport** | `FX` | Ascending pitch sweep dimensional teleport effect | `170-FX-Cosmic Teleport.xpl` |
| 171 | **Cymbal Cloud Riser** | `FX` | Thirty-six partial metal cloud riser with flanger and echoes | `171-FX-Cymbal Cloud Riser.xpl` |
| 172 | **Detonation Tail** | `FX` | Cinematic impact hit with sub drop and reverberant tail | `172-FX-Detonation Tail.xpl` |
| 173 | **Game Over Zap** | `FX` | Retro 8-bit arcade explosion zap one-shot | `173-FX-Game Over Zap.xpl` |
| 174 | **Gong Grain Swarm** | `FX` | Inharmonic metallic grain swarm effect | `174-FX-Gong Grain Swarm.xpl` |
| 175 | **Metal Gong Strike** | `FX` | Physical resonant metallic gong strike percussion | `175-FX-Metal Gong Strike.xpl` |
| 176 | **Noise Riser** | `FX` | Filtered white noise transitional build-up riser | `176-FX-Noise Riser.xpl` |
| 177 | **Reverse Gong Swell** | `FX` | Reversed gong swell effect | `177-FX-Reverse Gong Swell.xpl` |
| 178 | **Rising Grain Riser** | `FX` | Rising granular riser effect | `178-FX-Rising Grain Riser.xpl` |
| 179 | **Sci-Fi Laser Sweep** | `FX` | Downward pitch-modulated sci-fi laser burst | `179-FX-Sci-Fi Laser Sweep.xpl` |
| 180 | **Sonar Ping** | `FX` | Deep ocean submarine acoustic sonar ping | `180-FX-Sonar Ping.xpl` |
| 181 | **Spectral Chord Morph** | `FX` | Spectral chord morphing effect | `181-FX-Spectral Chord Morph.xpl` |
| 182 | **Sub Drop Boom** | `FX` | Subwoofer-testing downward pitch drop boom impact | `182-FX-Sub Drop Boom.xpl` |
| 183 | **Wind Sweep Riser** | `FX` | Noise band-sweep table wind riser on a fixed low cycle | `183-FX-Wind Sweep Riser.xpl` |

---

## Alphabetical Preset Index

| Preset Name | Category Group | # | Origin Folder |
|---|---|---|---|
| **808 Boom** | Bass Sub+808 | 000 | `Bass` |
| **Accordion Reed** | Strings+Brass | 146 | `Keys` |
| **Acid Saw Reso** | Bass Acid | 013 | `Bass` |
| **Acoustic Bore Reed** | Lead Synth | 033 | `Lead` |
| **Acoustic Style Pluck** | Pluck | 118 | `Pluck` |
| **Air Chiff Flute** | Lead Synth | 034 | `Lead` |
| **Alien Communication** | Texture | 160 | `FX` |
| **Analog Sweep Bass** | Bass Synth | 020 | `Bass` |
| **Aux Keyscale Pluck** | Pluck | 119 | `Pluck` |
| **B3 Drawbar Organ** | Keys | 100 | `Keys` |
| **Bell Chime Lead** | Bells+Mallets | 132 | `Lead` |
| **Bell FM Ripple Lead** | Lead Sync+FM | 061 | `Lead` |
| **Bell Mallet Pluck** | Bells+Mallets | 133 | `Pluck` |
| **Bend Formant Growl** | Bass Acid | 014 | `Bass` |
| **Bode Barber Riser** | FX | 169 | `FX` |
| **Bode Shifter Drone** | Texture | 161 | `FX` |
| **Bowed MPE Strings** | Strings+Brass | 147 | `Pad` |
| **Bowed Pressure Strings** | Strings+Brass | 148 | `Pad` |
| **Brass Swell Ensemble** | Strings+Brass | 149 | `Pad` |
| **Breakbeat Slice Keys** | Keys | 101 | `Keys` |
| **Breath Flute Lead** | Lead Synth | 035 | `Lead` |
| **Breath of Machines** | Texture | 162 | `FX` |
| **Cathedral Bell Ring** | Bells+Mallets | 134 | `Keys` |
| **Cathedral Choir** | Pad Warm | 069 | `Pad` |
| **Celesta Bells** | Bells+Mallets | 135 | `Keys` |
| **Chime Drop** | Bells+Mallets | 136 | `Pluck` |
| **Choir Swell Ensemble** | Pad Warm | 070 | `Pad` |
| **Chord Cloud Pad** | Pad Evolving | 081 | `Pad` |
| **Chorused Saw Solo** | Lead Synth | 036 | `Lead` |
| **Clavinet Funk** | Keys | 102 | `Keys` |
| **Click Attack Bass** | Bass Synth | 021 | `Bass` |
| **Comb Metal Bass** | Bass Synth | 022 | `Bass` |
| **Comb Reso Swell** | Pad Evolving | 082 | `Pad` |
| **Comb Zither** | Pluck | 120 | `Pluck` |
| **Cosmic Drift Lead** | Lead Synth | 037 | `Lead` |
| **Cosmic Teleport** | FX | 170 | `FX` |
| **Custom Shape Wobble** | Bass Reese | 007 | `Bass` |
| **Cymbal Cloud Riser** | FX | 171 | `FX` |
| **Dark Cinema Drone** | Texture | 163 | `Pad` |
| **Deep Sea Ambient** | Pad Evolving | 083 | `Pad` |
| **Detonation Tail** | FX | 172 | `FX` |
| **Diffusor Cloud Pad** | Pad Evolving | 084 | `Pad` |
| **Digital Glass Tap** | Pluck | 121 | `Pluck` |
| **Diode Bite Lead** | Lead Synth | 038 | `Lead` |
| **Diode Saturated Bass** | Bass Acid | 015 | `Bass` |
| **Diphthong Morph Vox** | Lead Synth | 039 | `Lead` |
| **Dirty Distortion Bass** | Bass Acid | 016 | `Bass` |
| **Dirty Peak Lead** | Lead Synth | 040 | `Lead` |
| **Dispersion Phase Pad** | Pad Evolving | 085 | `Pad` |
| **Drawbar Organ Keys** | Keys | 103 | `Keys` |
| **Dream Grand Piano** | Keys | 104 | `Keys` |
| **Dual Octave Hero** | Lead Synth | 041 | `Lead` |
| **Dusty Rhodes Keys** | Keys | 105 | `Keys` |
| **Dyno Electric Piano** | Keys | 106 | `Keys` |
| **Electric Mute Pluck** | Pluck | 122 | `Pluck` |
| **EMS Squelch Bass** | Bass Acid | 017 | `Bass` |
| **Ethereal Glass** | Pad Evolving | 086 | `Pad` |
| **Fast Arp Bass** | Sequence | 154 | `Bass` |
| **Fat Reese** | Bass Reese | 008 | `Bass` |
| **FM Chain Tine Keys** | Keys | 107 | `Keys` |
| **FM Digital Lead** | Lead Sync+FM | 062 | `Lead` |
| **FM Soft Organ** | Keys | 108 | `Keys` |
| **Formant Pop Pluck** | Pluck | 123 | `Pluck` |
| **Formant Vowel Bass** | Bass Synth | 023 | `Bass` |
| **Free Bar Vibraphone** | Bells+Mallets | 137 | `Keys` |
| **Frozen Aurora** | Pad Evolving | 087 | `Pad` |
| **Frozen Pluck Drone** | Pad Evolving | 088 | `Pad` |
| **Game Over Zap** | FX | 173 | `FX` |
| **Gentle Flute Pad** | Pad Warm | 071 | `Pad` |
| **Glass Organ Tones** | Keys | 109 | `Keys` |
| **Glass Ping Pluck** | Pluck | 124 | `Pluck` |
| **Glass Zone Bells** | Bells+Mallets | 138 | `Keys` |
| **Golden Dawn Pad** | Pad Warm | 072 | `Pad` |
| **Gong Bloom Pad** | Pad Evolving | 089 | `Pad` |
| **Gong Grain Swarm** | FX | 174 | `FX` |
| **Gritty Square Bass** | Bass Synth | 024 | `Bass` |
| **Harmonic Cloud** | Pad Evolving | 090 | `Pad` |
| **Harmonic Shimmer Lead** | Lead Synth | 042 | `Lead` |
| **Harmonic Sine Bass** | Bass Sub+808 | 001 | `Bass` |
| **Harmonic Tine Pluck** | Pluck | 125 | `Pluck` |
| **Harmonic Wah Bass** | Bass Synth | 025 | `Bass` |
| **Harp Drops** | Pluck | 126 | `Pluck` |
| **Harpsichord Bar** | Strings+Brass | 150 | `Keys` |
| **Hyper Dimension Pad** | Pad Warm | 073 | `Pad` |
| **Hyper Saw Lead** | Lead Synth | 043 | `Lead` |
| **Infinite Horizon** | Pad Evolving | 091 | `Pad` |
| **Just Intonation Keys** | Keys | 110 | `Keys` |
| **Kalimba Tines** | Bells+Mallets | 139 | `Pluck` |
| **Karplus Harp** | Pluck | 127 | `Pluck` |
| **Ladder FM Bass** | Bass Synth | 026 | `Bass` |
| **Laser Beam Lead** | Lead Sync+FM | 063 | `Lead` |
| **Lo-Fi Rhodes** | Keys | 111 | `Keys` |
| **Lorenz Garden** | Pad Evolving | 092 | `Pad` |
| **Lush Analog Pad** | Pad Warm | 074 | `Pad` |
| **Marimba Layer Keys** | Bells+Mallets | 140 | `Keys` |
| **Marimba Synth** | Bells+Mallets | 141 | `Pluck` |
| **Metal Gong Strike** | FX | 175 | `FX` |
| **Metallic Edge Lead** | Lead Synth | 044 | `Lead` |
| **Mid Side Halo** | Pad Warm | 075 | `Pad` |
| **Moog Ladder Bass** | Bass Synth | 027 | `Bass` |
| **MPE Pressure Lead** | Lead Synth | 045 | `Lead` |
| **Muted Honky Tonk** | Keys | 112 | `Keys` |
| **Noise Riser** | FX | 176 | `FX` |
| **Nylon Zone Pluck** | Pluck | 128 | `Pluck` |
| **Odd Harmonics Bass** | Bass Synth | 028 | `Bass` |
| **Odd Harmonics Pad** | Pad Warm | 076 | `Pad` |
| **Parallel Dusk Pad** | Pad Evolving | 093 | `Pad` |
| **PD Reso Acid Bass** | Bass Acid | 018 | `Bass` |
| **Pentatonic Step Pluck** | Sequence | 155 | `Pluck` |
| **Phaser Sweep Lead** | Lead Synth | 046 | `Lead` |
| **Pink Noise Sub** | Bass Sub+808 | 002 | `Bass` |
| **Pitch Drop 808** | Bass Sub+808 | 003 | `Bass` |
| **Pizzicato Strings** | Strings+Brass | 151 | `Pluck` |
| **Plucked Lead Tone** | Lead Synth | 047 | `Lead` |
| **Pulse Width Bass** | Bass Synth | 029 | `Bass` |
| **Pulse Width Mod Lead** | Lead Synth | 048 | `Lead` |
| **Punchy Pluck Bass** | Bass Synth | 030 | `Bass` |
| **Radiant Pulse Pad** | Sequence | 156 | `Pad` |
| **Reese Dual Phase Bass** | Bass Reese | 009 | `Bass` |
| **Reverse Gong Swell** | FX | 177 | `FX` |
| **Ring Mod Gamelan** | Bells+Mallets | 142 | `Pluck` |
| **Rising Grain Riser** | FX | 178 | `FX` |
| **Rotary Rock Organ** | Keys | 113 | `Keys` |
| **Rubber Bass Pluck** | Pluck | 129 | `Pluck` |
| **Sci-Fi Laser Sweep** | FX | 179 | `FX` |
| **Scream Filter Bass** | Bass Reese | 010 | `Bass` |
| **Screaming Sync Lead** | Lead Sync+FM | 064 | `Lead` |
| **Shepard Ascent** | Texture | 164 | `Pad` |
| **Shimmer Bell Walk** | Bells+Mallets | 143 | `Keys` |
| **Slow Motion Waves** | Pad Evolving | 094 | `Pad` |
| **Solid Sub** | Bass Sub+808 | 004 | `Bass` |
| **Solo Square Lead** | Lead Synth | 049 | `Lead` |
| **Sonar Ping** | FX | 180 | `FX` |
| **Soprano Vowel Pad** | Pad Warm | 077 | `Pad` |
| **Space Odyssey Pad** | Pad Evolving | 095 | `Pad` |
| **Space Wind Storm** | Texture | 165 | `FX` |
| **Space Wobble Bass** | Bass Reese | 011 | `Bass` |
| **Spectral Bowed Glass** | Lead Synth | 050 | `Lead` |
| **Spectral Chord Morph** | FX | 181 | `FX` |
| **Spectral Gong Pad** | Pad Evolving | 096 | `Pad` |
| **Spectral Vowel Freeze** | Pad Evolving | 097 | `Pad` |
| **Spruce Guitar** | Pluck | 130 | `Pluck` |
| **Staccato Bleep Lead** | Lead Synth | 051 | `Lead` |
| **Starlight Shimmer** | Pad Warm | 078 | `Pad` |
| **Step Morph Pluck** | Sequence | 157 | `Pluck` |
| **Stretched Glass Choir** | Pad Evolving | 098 | `Pad` |
| **Stutter Grain Kit** | Pluck | 131 | `Pluck` |
| **Sub Drop Boom** | FX | 182 | `FX` |
| **Sub Duo Octave** | Bass Sub+808 | 005 | `Bass` |
| **Subterranean Duo** | Bass Sub+808 | 006 | `Bass` |
| **Subtle Tape Drift** | Pad Warm | 079 | `Pad` |
| **Sync Cry Lead** | Lead Sync+FM | 065 | `Lead` |
| **Sync Growl Bass** | Bass Acid | 019 | `Bass` |
| **Sync Saw Raw Bass** | Bass Synth | 031 | `Bass` |
| **Sync Sine Screamer** | Lead Sync+FM | 066 | `Lead` |
| **Talk Box Baritone** | Lead Synth | 052 | `Lead` |
| **Tempo Gate Chords** | Sequence | 158 | `Keys` |
| **Throat Singer Drone** | Pad Evolving | 099 | `Pad` |
| **Tine Glass Keys** | Keys | 114 | `Keys` |
| **Trance Short Pluck** | Sequence | 159 | `Pluck` |
| **Trance Stack Lead** | Lead Synth | 053 | `Lead` |
| **Tri Soft Solo** | Lead Synth | 054 | `Lead` |
| **Tri-Band Split Texture** | Texture | 166 | `Pad` |
| **Triple Fifth Lead** | Lead Synth | 055 | `Lead` |
| **Triple Fold Reese** | Bass Reese | 012 | `Bass` |
| **Twin Warp Lead** | Lead Sync+FM | 067 | `Lead` |
| **Utility Width Keys** | Keys | 115 | `Keys` |
| **Velvet Strings** | Strings+Brass | 152 | `Pad` |
| **Vibraphone Bars** | Bells+Mallets | 144 | `Keys` |
| **Vintage Brass Pad** | Strings+Brass | 153 | `Pad` |
| **Vintage Electric Tine** | Keys | 116 | `Keys` |
| **Vinyl Crackle Bed** | Texture | 167 | `FX` |
| **Vinyl Dust Crackle** | Texture | 168 | `FX` |
| **Vocaloid Lead** | Lead Synth | 056 | `Lead` |
| **Vowel Choir Pad** | Pad Warm | 080 | `Pad` |
| **Vowel Scrub Choir** | Lead Synth | 057 | `Lead` |
| **Vowel Table Lead** | Lead Synth | 058 | `Lead` |
| **Vowel Whistle Lead** | Lead Synth | 059 | `Lead` |
| **Wah Pressure Clav** | Keys | 117 | `Keys` |
| **Warm Analog Bass** | Bass Synth | 032 | `Bass` |
| **Warm Toy Piano** | Bells+Mallets | 145 | `Keys` |
| **Warm Vintage Lead** | Lead Synth | 060 | `Lead` |
| **West Coast Wavefold** | Lead Sync+FM | 068 | `Lead` |
| **Wind Sweep Riser** | FX | 183 | `FX` |
