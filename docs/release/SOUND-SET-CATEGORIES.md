# Sound Set Browser Categories: Review & Mapping

Status: **Proposed for review by the owner.**

This document details the browser category organization for SynthyBoy factory presets in the Akai MPC host Sounds browser. Presets are grouped in a musician-friendly manner into 15 focused sub-styles (maximum 16 groups, all groups contain at least 4 presets).

## Browser Rules & Stock Conventions

- **Category Name Characters**: Stock Akai MPC instruments avoid slashes (`/`) and punctuation in category names, using letters, spaces, digits, and `+` (e.g. `Synth Bass`, `Plucks+Synths`, `Pitched Perc+Bells`). Categories here follow this rule exactly (e.g. `Bass Sub+808`, `Lead Sync+FM`, `Bells+Mallets`, `Strings+Brass`).
- **File Naming & Numbering**: `scripts/gen-xpl-presets.py` names files as `NNN-<Category>-<Preset Name>.xpl`. Files are numbered sequentially (`000` .. `135`) such that presets within each group stay strictly contiguous in the group list order, and presets within each group sort alphabetically.
- **Integrity & Extension**: `scripts/assign-preset-categories.py --check` enforces that all presets exist, all groups meet the 4-preset minimum, and all names are FAT/stock-compliant. `--suggest` provides deterministic recommendations for future sound set additions.

## Group Summary

| # | Group Name | Presets | Origin Folders | Musical Style / Sound Character |
|---|---|---|---|---|
| 01 | **Bass Sub+808** | 6 | Bass | Deep pure sine sub-bass, 808 pitch-drop booms, and low-end foundations |
| 02 | **Bass Reese** | 5 | Bass | Detuned multi-voice saw basslines, wavefolded reeses, and rhythmic wobbles |
| 03 | **Bass Acid** | 6 | Bass | 303-style resonant squelches, diode saturation, distortion, and aggressive growls |
| 04 | **Bass Synth** | 9 | Bass | General analog, punchy transient, hollow funk, and pulse-width basslines |
| 05 | **Lead Synth** | 18 | Lead | Solo anthems, supersaws, expressive analog, and vocal formant leads |
| 06 | **Lead Sync+FM** | 6 | Lead | Hard-sync pitched oscillators, frequency-modulated digital leads, and warp cutters |
| 07 | **Pad Warm** | 10 | Pad | Classic analog strings, warm choirs, vintage brass swells, and lush chord beds |
| 08 | **Pad Evolving** | 12 | Pad | Slow morphing wavetables, ambient diffusor washes, and shifting spectral clouds |
| 09 | **Keys** | 14 | Keys | Acoustic grands, Rhodes/dyno electric pianos, drawbar/rock organs, and clavs |
| 10 | **Pluck** | 12 | Lead, Pluck | Acoustic guitars, Karplus-Strong physical strings, zithers, and transient plucks |
| 11 | **Bells+Mallets** | 9 | Keys, Lead, Pluck | Tuned percussion, celestas, vibraphones, marimbas, kalimbas, and struck gamelans |
| 12 | **Strings+Brass** | 7 | Keys, Pad, Pluck | Orchestral string ensembles, brass sections, accordions, and baroque harpsichords |
| 13 | **Sequence** | 5 | Bass, Keys, Pad, Pluck | Self-playing 16-step pentatonic grooves, tempo-synced chord gates, and fast arps |
| 14 | **Texture** | 8 | FX, Pad | Continuous atmospheric noise beds, vinyl crackle, drones, and ambient windscapes |
| 15 | **FX** | 9 | FX | Transitional risers, pitch sweeps, teleport zaps, impacts, and one-shot effects |

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

### 2. Bass Reese (5 presets)

*Detuned multi-voice saw basslines, wavefolded reeses, and rhythmic wobbles*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 006 | **Custom Shape Wobble** | `Bass` | Detuned dual-saw bass with rhythmic hand-drawn wobble LFO | `006-Bass Reese-Custom Shape Wobble.xpl` |
| 007 | **Fat Reese** | `Bass` | 5-voice detuned sawtooth Reese bass for drum and bass | `007-Bass Reese-Fat Reese.xpl` |
| 008 | **Scream Filter Bass** | `Bass` | 3-voice detuned saw bass driven through scream filter | `008-Bass Reese-Scream Filter Bass.xpl` |
| 009 | **Space Wobble Bass** | `Bass` | Dub-style wobble bass with LFO-modulated lowpass cutoff | `009-Bass Reese-Space Wobble Bass.xpl` |
| 010 | **Triple Fold Reese** | `Bass` | 3-oscillator detuned saw reese with sine-fold wavefolding | `010-Bass Reese-Triple Fold Reese.xpl` |

### 3. Bass Acid (6 presets)

*303-style resonant squelches, diode saturation, distortion, and aggressive growls*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 011 | **Acid Saw Reso** | `Bass` | Classic 303-style resonant sawtooth acid line | `011-Bass Acid-Acid Saw Reso.xpl` |
| 012 | **Bend Formant Growl** | `Bass` | Aggressive bent saw growl through parallel formant and analog filters | `012-Bass Acid-Bend Formant Growl.xpl` |
| 013 | **Diode Saturated Bass** | `Bass` | Warm saturated diode ladder filter resonant bass | `013-Bass Acid-Diode Saturated Bass.xpl` |
| 014 | **Dirty Distortion Bass** | `Bass` | Overdriven industrial bass using dirty filter model | `014-Bass Acid-Dirty Distortion Bass.xpl` |
| 015 | **EMS Squelch Bass** | `Bass` | Resonant EMS ladder bass with short snappy filter envelope | `015-Bass Acid-EMS Squelch Bass.xpl` |
| 016 | **Sync Growl Bass** | `Bass` | Hard-sync modulated aggressive growl bass | `016-Bass Acid-Sync Growl Bass.xpl` |

### 4. Bass Synth (9 presets)

*General analog, punchy transient, hollow funk, and pulse-width basslines*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 017 | **Click Attack Bass** | `Bass` | Punchy synth bass with transient click attack | `017-Bass Synth-Click Attack Bass.xpl` |
| 018 | **Comb Metal Bass** | `Bass` | Physical resonant comb filter metallic string bass | `018-Bass Synth-Comb Metal Bass.xpl` |
| 019 | **Formant Vowel Bass** | `Bass` | Formant filter sweeping vocal vowel bassline | `019-Bass Synth-Formant Vowel Bass.xpl` |
| 020 | **Gritty Square Bass** | `Bass` | Raw edgy lo-fi square wave synth bass | `020-Bass Synth-Gritty Square Bass.xpl` |
| 021 | **Moog Ladder Bass** | `Bass` | Vintage analog ladder filter round bass | `021-Bass Synth-Moog Ladder Bass.xpl` |
| 022 | **Odd Harmonics Bass** | `Bass` | Hollow funk bass driven by odd harmonic series | `022-Bass Synth-Odd Harmonics Bass.xpl` |
| 023 | **Pulse Width Bass** | `Bass` | PWM modulated pulse wave electro bass | `023-Bass Synth-Pulse Width Bass.xpl` |
| 024 | **Punchy Pluck Bass** | `Bass` | Tight transient bass with short percussive decay | `024-Bass Synth-Punchy Pluck Bass.xpl` |
| 025 | **Warm Analog Bass** | `Bass` | Classic deep house warm analog lowpass bass | `025-Bass Synth-Warm Analog Bass.xpl` |

### 5. Lead Synth (18 presets)

*Solo anthems, supersaws, expressive analog, and vocal formant leads*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 026 | **Chorused Saw Solo** | `Lead` | Bright sawtooth solo lead bathed in stereo chorus | `026-Lead Synth-Chorused Saw Solo.xpl` |
| 027 | **Cosmic Drift Lead** | `Lead` | Slowly drifting ambient analog solo synth lead | `027-Lead Synth-Cosmic Drift Lead.xpl` |
| 028 | **Diode Bite Lead** | `Lead` | Sharp biting diode ladder filtered aggressive lead | `028-Lead Synth-Diode Bite Lead.xpl` |
| 029 | **Dirty Peak Lead** | `Lead` | Distorted resonant peak filter industrial synth lead | `029-Lead Synth-Dirty Peak Lead.xpl` |
| 030 | **Dual Octave Hero** | `Lead` | Heroic stacked octave dual sawtooth anthem lead | `030-Lead Synth-Dual Octave Hero.xpl` |
| 031 | **Harmonic Shimmer Lead** | `Lead` | Upper harmonic rich shimmering solo synth lead | `031-Lead Synth-Harmonic Shimmer Lead.xpl` |
| 032 | **Hyper Saw Lead** | `Lead` | 7-voice detuned supersaw stadium anthem lead | `032-Lead Synth-Hyper Saw Lead.xpl` |
| 033 | **Metallic Edge Lead** | `Lead` | Aggressive electro lead with metallic overtone edge | `033-Lead Synth-Metallic Edge Lead.xpl` |
| 034 | **MPE Pressure Lead** | `Lead` | Expressive MPE solo lead with per-note pressure filter modulation | `034-Lead Synth-MPE Pressure Lead.xpl` |
| 035 | **Phaser Sweep Lead** | `Lead` | Spacey multi-stage phaser swept solo synth lead | `035-Lead Synth-Phaser Sweep Lead.xpl` |
| 036 | **Pulse Width Mod Lead** | `Lead` | Classic 80s chorus-widened pulse-width modulated lead | `036-Lead Synth-Pulse Width Mod Lead.xpl` |
| 037 | **Solo Square Lead** | `Lead` | Expressive monophonic retro square wave solo lead | `037-Lead Synth-Solo Square Lead.xpl` |
| 038 | **Staccato Bleep Lead** | `Lead` | Retro arcade-inspired punchy staccato bleep melody lead | `038-Lead Synth-Staccato Bleep Lead.xpl` |
| 039 | **Tri Soft Solo** | `Lead` | Flute-like soft triangle wave solo lead | `039-Lead Synth-Tri Soft Solo.xpl` |
| 040 | **Triple Fifth Lead** | `Lead` | Stacked 3-oscillator power-chord lead with fifth and octave layers | `040-Lead Synth-Triple Fifth Lead.xpl` |
| 041 | **Vocaloid Lead** | `Lead` | Singing formant vocal tract lead synthesizer | `041-Lead Synth-Vocaloid Lead.xpl` |
| 042 | **Vowel Whistle Lead** | `Lead` | Mono legato vocal formant morph lead with expressive vibrato | `042-Lead Synth-Vowel Whistle Lead.xpl` |
| 043 | **Warm Vintage Lead** | `Lead` | Smooth vintage analog lowpass filtered solo lead | `043-Lead Synth-Warm Vintage Lead.xpl` |

### 6. Lead Sync+FM (6 presets)

*Hard-sync pitched oscillators, frequency-modulated digital leads, and warp cutters*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 044 | **Bell FM Ripple Lead** | `Lead` | Sine carrier phase-modulated FM bell lead with velocity bite | `044-Lead Sync+FM-Bell FM Ripple Lead.xpl` |
| 045 | **FM Digital Lead** | `Lead` | Crisp digital FM synthesis lead with sharp modulation | `045-Lead Sync+FM-FM Digital Lead.xpl` |
| 046 | **Laser Beam Lead** | `Lead` | Futuristic cutting sync and warp modulation laser lead | `046-Lead Sync+FM-Laser Beam Lead.xpl` |
| 047 | **Screaming Sync Lead** | `Lead` | Aggressive pitched oscillator hard-sync rock lead | `047-Lead Sync+FM-Screaming Sync Lead.xpl` |
| 048 | **Sync Cry Lead** | `Lead` | Expressive singing hard-sync saw lead with envelope sweep | `048-Lead Sync+FM-Sync Cry Lead.xpl` |
| 049 | **Twin Warp Lead** | `Lead` | Dual sync and bend warp stages in series with LFO sweep | `049-Lead Sync+FM-Twin Warp Lead.xpl` |

### 7. Pad Warm (10 presets)

*Classic analog strings, warm choirs, vintage brass swells, and lush chord beds*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 050 | **Cathedral Choir** | `Pad` | Vast reverberant sacred choir pad | `050-Pad Warm-Cathedral Choir.xpl` |
| 051 | **Gentle Flute Pad** | `Pad` | Soft breathy woodwind flute ensemble pad | `051-Pad Warm-Gentle Flute Pad.xpl` |
| 052 | **Golden Dawn Pad** | `Pad` | Peaceful warm morning dawn analog pad | `052-Pad Warm-Golden Dawn Pad.xpl` |
| 053 | **Hyper Dimension Pad** | `Pad` | Sine-to-saw pad expanded with Hyper/Dimension width | `053-Pad Warm-Hyper Dimension Pad.xpl` |
| 054 | **Lush Analog Pad** | `Pad` | Warm enveloping classic analog string/synth pad | `054-Pad Warm-Lush Analog Pad.xpl` |
| 055 | **Mid Side Halo** | `Pad` | 3-oscillator warm pad with mid/side processing and wide halo | `055-Pad Warm-Mid Side Halo.xpl` |
| 056 | **Odd Harmonics Pad** | `Pad` | Warm hollow wooden pipe organ pad with odd harmonics | `056-Pad Warm-Odd Harmonics Pad.xpl` |
| 057 | **Starlight Shimmer** | `Pad` | Bright crystalline shimmering high-register synth pad | `057-Pad Warm-Starlight Shimmer.xpl` |
| 058 | **Subtle Tape Drift** | `Pad` | Vintage warm pad with subtle tape flutter and pitch drift | `058-Pad Warm-Subtle Tape Drift.xpl` |
| 059 | **Vowel Choir Pad** | `Pad` | Synthetic choral pad with warm formant vowel blending | `059-Pad Warm-Vowel Choir Pad.xpl` |

### 8. Pad Evolving (12 presets)

*Slow morphing wavetables, ambient diffusor washes, and shifting spectral clouds*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 060 | **Comb Reso Swell** | `Pad` | Acoustic-like resonant comb filter swelling wash | `060-Pad Evolving-Comb Reso Swell.xpl` |
| 061 | **Deep Sea Ambient** | `Pad` | Submerged oceanic atmosphere with slow filter movement | `061-Pad Evolving-Deep Sea Ambient.xpl` |
| 062 | **Diffusor Cloud Pad** | `Pad` | Detuned saw pad smeared by Diffusor filter into slow drift | `062-Pad Evolving-Diffusor Cloud Pad.xpl` |
| 063 | **Ethereal Glass** | `Pad` | Airy crystalline glass texture with slowly shifting overtones | `063-Pad Evolving-Ethereal Glass.xpl` |
| 064 | **Frozen Aurora** | `Pad` | Cold crystalline northern lights atmospheric soundscape | `064-Pad Evolving-Frozen Aurora.xpl` |
| 065 | **Harmonic Cloud** | `Pad` | Dense evolving harmonic cluster pad with subtle modulation | `065-Pad Evolving-Harmonic Cloud.xpl` |
| 066 | **Infinite Horizon** | `Pad` | Massive cinematic landscape pad with endless evolving decay | `066-Pad Evolving-Infinite Horizon.xpl` |
| 067 | **Lorenz Garden** | `Pad` | Generative bell pad with chaotic Lorenz and Rossler LFO drift | `067-Pad Evolving-Lorenz Garden.xpl` |
| 068 | **Parallel Dusk Pad** | `Pad` | Dual parallel stereo filters with opposing LFO cutoff motion | `068-Pad Evolving-Parallel Dusk Pad.xpl` |
| 069 | **Slow Motion Waves** | `Pad` | Slow morphing wavetable sweep across spectral frames | `069-Pad Evolving-Slow Motion Waves.xpl` |
| 070 | **Space Odyssey Pad** | `Pad` | Expansive sci-fi atmospheric pad with cosmic movement | `070-Pad Evolving-Space Odyssey Pad.xpl` |
| 071 | **Stretched Glass Choir** | `Pad` | Spectral-stretched partials drifting across random LFOs | `071-Pad Evolving-Stretched Glass Choir.xpl` |

### 9. Keys (14 presets)

*Acoustic grands, Rhodes/dyno electric pianos, drawbar/rock organs, and clavs*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 072 | **B3 Drawbar Organ** | `Keys` | Warm tonewheel jazz drawbar organ | `072-Keys-B3 Drawbar Organ.xpl` |
| 073 | **Clavinet Funk** | `Keys` | Funky 70s electronic clavinet keyboard | `073-Keys-Clavinet Funk.xpl` |
| 074 | **Dream Grand Piano** | `Keys` | Acoustic grand piano style dynamic keyboard | `074-Keys-Dream Grand Piano.xpl` |
| 075 | **Dyno Electric Piano** | `Keys` | Bright modified 80s dyno tine electric piano | `075-Keys-Dyno Electric Piano.xpl` |
| 076 | **FM Soft Organ** | `Keys` | Smooth gospel tonewheel organ using FM synthesis | `076-Keys-FM Soft Organ.xpl` |
| 077 | **Glass Organ Tones** | `Keys` | Crystalline cathedral glass church organ | `077-Keys-Glass Organ Tones.xpl` |
| 078 | **Just Intonation Keys** | `Keys` | Pure harmonic-series keyboard tuned in just intonation | `078-Keys-Just Intonation Keys.xpl` |
| 079 | **Lo-Fi Rhodes** | `Keys` | Mellow chillhop Rhodes electric piano with warm tape saturation | `079-Keys-Lo-Fi Rhodes.xpl` |
| 080 | **Muted Honky Tonk** | `Keys` | Saloon-style upright piano with detuned unison tines | `080-Keys-Muted Honky Tonk.xpl` |
| 081 | **Rotary Rock Organ** | `Keys` | Gritty rock organ through rotary Leslie speaker chorus | `081-Keys-Rotary Rock Organ.xpl` |
| 082 | **Tine Glass Keys** | `Keys` | Electric piano combining FM sine body with glassy bell tine ping | `082-Keys-Tine Glass Keys.xpl` |
| 083 | **Utility Width Keys** | `Keys` | Chorused triangle and square keys with mono low-end utility | `083-Keys-Utility Width Keys.xpl` |
| 084 | **Vintage Electric Tine** | `Keys` | Classic warm Rhodes electric piano with soft tines | `084-Keys-Vintage Electric Tine.xpl` |
| 085 | **Wah Pressure Clav** | `Keys` | Expressive funk clavinet with pad pressure sweeping wah filter | `085-Keys-Wah Pressure Clav.xpl` |

### 10. Pluck (12 presets)

*Acoustic guitars, Karplus-Strong physical strings, zithers, and transient plucks*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 086 | **Acoustic Style Pluck** | `Pluck` | Crisp acoustic guitar transient steel-string pluck | `086-Pluck-Acoustic Style Pluck.xpl` |
| 087 | **Aux Keyscale Pluck** | `Pluck` | Plucked synth with keyboard-scaled dynamic filter sweep | `087-Pluck-Aux Keyscale Pluck.xpl` |
| 088 | **Comb Zither** | `Pluck` | Physical modeled plucked zither string using comb resonance | `088-Pluck-Comb Zither.xpl` |
| 089 | **Digital Glass Tap** | `Pluck` | Modern UI glassy transient percussive tap tone | `089-Pluck-Digital Glass Tap.xpl` |
| 090 | **Electric Mute Pluck** | `Pluck` | Palm-muted electric guitar style synth pluck | `090-Pluck-Electric Mute Pluck.xpl` |
| 091 | **Formant Pop Pluck** | `Pluck` | Short staccato vocal formant pop pluck | `091-Pluck-Formant Pop Pluck.xpl` |
| 092 | **Harmonic Tine Pluck** | `Pluck` | Rich harmonic struck tine transient pluck | `092-Pluck-Harmonic Tine Pluck.xpl` |
| 093 | **Harp Drops** | `Pluck` | Fluid crystalline concert harp plucked string drops | `093-Pluck-Harp Drops.xpl` |
| 094 | **Karplus Harp** | `Pluck` | Karplus-Strong physical string model excited by noise burst | `094-Pluck-Karplus Harp.xpl` |
| 095 | **Plucked Lead Tone** | `Lead` | Hybrid fast pluck transient with sustained body | `095-Pluck-Plucked Lead Tone.xpl` |
| 096 | **Rubber Bass Pluck** | `Pluck` | Bouncy rubberized synth pluck with elastic filter response | `096-Pluck-Rubber Bass Pluck.xpl` |
| 097 | **Spruce Guitar** | `Pluck` | Clean acoustic guitar pluck with warm spruce resonance | `097-Pluck-Spruce Guitar.xpl` |

### 11. Bells+Mallets (9 presets)

*Tuned percussion, celestas, vibraphones, marimbas, kalimbas, and struck gamelans*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 098 | **Bell Chime Lead** | `Lead` | Crystalline bell overtones and struck chime lead | `098-Bells+Mallets-Bell Chime Lead.xpl` |
| 099 | **Bell Mallet Pluck** | `Pluck` | Percussive bell mallet strike with metallic overtones | `099-Bells+Mallets-Bell Mallet Pluck.xpl` |
| 100 | **Celesta Bells** | `Keys` | Fairytale orchestral celesta bell keyboard | `100-Bells+Mallets-Celesta Bells.xpl` |
| 101 | **Chime Drop** | `Pluck` | Delicate falling metallic glass chime cluster | `101-Bells+Mallets-Chime Drop.xpl` |
| 102 | **Kalimba Tines** | `Pluck` | African wooden thumb piano struck metal tines | `102-Bells+Mallets-Kalimba Tines.xpl` |
| 103 | **Marimba Synth** | `Pluck` | Warm struck wooden marimba bar resonance | `103-Bells+Mallets-Marimba Synth.xpl` |
| 104 | **Ring Mod Gamelan** | `Pluck` | Inharmonic metallic gamelan bar strike with ring modulation | `104-Bells+Mallets-Ring Mod Gamelan.xpl` |
| 105 | **Vibraphone Bars** | `Keys` | Warm jazz vibraphone struck metal bars with tremolo | `105-Bells+Mallets-Vibraphone Bars.xpl` |
| 106 | **Warm Toy Piano** | `Keys` | Lo-fi indie toy piano with struck bell-like tines | `106-Bells+Mallets-Warm Toy Piano.xpl` |

### 12. Strings+Brass (7 presets)

*Orchestral string ensembles, brass sections, accordions, and baroque harpsichords*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 107 | **Accordion Reed** | `Keys` | Dual reed acoustic folk accordion emulation | `107-Strings+Brass-Accordion Reed.xpl` |
| 108 | **Bowed Pressure Strings** | `Pad` | Expressive bowed string ensemble with pad pressure vibrato | `108-Strings+Brass-Bowed Pressure Strings.xpl` |
| 109 | **Brass Swell Ensemble** | `Pad` | 3-oscillator swelling brass ensemble with filter dynamics | `109-Strings+Brass-Brass Swell Ensemble.xpl` |
| 110 | **Harpsichord Bar** | `Keys` | Crisp baroque plucked harpsichord strings | `110-Strings+Brass-Harpsichord Bar.xpl` |
| 111 | **Pizzicato Strings** | `Pluck` | Orchestral string section short plucked pizzicato | `111-Strings+Brass-Pizzicato Strings.xpl` |
| 112 | **Velvet Strings** | `Pad` | Silky smooth orchestral synth string section | `112-Strings+Brass-Velvet Strings.xpl` |
| 113 | **Vintage Brass Pad** | `Pad` | Classic 80s warm synth brass ensemble pad | `113-Strings+Brass-Vintage Brass Pad.xpl` |

### 13. Sequence (5 presets)

*Self-playing 16-step pentatonic grooves, tempo-synced chord gates, and fast arps*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 114 | **Fast Arp Bass** | `Bass` | Fast sequenced trance arpeggio bassline | `114-Sequence-Fast Arp Bass.xpl` |
| 115 | **Pentatonic Step Pluck** | `Pluck` | Self-playing 16-step pentatonic arpeggio sequence with synced LFO | `115-Sequence-Pentatonic Step Pluck.xpl` |
| 116 | **Radiant Pulse Pad** | `Pad` | Pulsing rhythmic pad with tempo-synced chord pulse | `116-Sequence-Radiant Pulse Pad.xpl` |
| 117 | **Tempo Gate Chords** | `Keys` | Chords rhythmic chopped by a 16-step tempo-synced gate LFO | `117-Sequence-Tempo Gate Chords.xpl` |
| 118 | **Trance Short Pluck** | `Pluck` | Punchy sequenced trance groove pluck with tight decay | `118-Sequence-Trance Short Pluck.xpl` |

### 14. Texture (8 presets)

*Continuous atmospheric noise beds, vinyl crackle, drones, and ambient windscapes*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 119 | **Alien Communication** | `FX` | Resonant formant alien vocal chatter soundscape | `119-Texture-Alien Communication.xpl` |
| 120 | **Bode Shifter Drone** | `FX` | Slow metallic feedback drone through Bode frequency shifter | `120-Texture-Bode Shifter Drone.xpl` |
| 121 | **Breath of Machines** | `FX` | Breathing industrial atmosphere with formant noise and Lorenz LFO | `121-Texture-Breath of Machines.xpl` |
| 122 | **Dark Cinema Drone** | `Pad` | Low suspenseful cinematic atmospheric drone | `122-Texture-Dark Cinema Drone.xpl` |
| 123 | **Shepard Ascent** | `Pad` | Endless ascending Shepard tone drone with Lorenz LFO modulation | `123-Texture-Shepard Ascent.xpl` |
| 124 | **Space Wind Storm** | `FX` | Howling ambient cosmic wind noise soundscape | `124-Texture-Space Wind Storm.xpl` |
| 125 | **Tri-Band Split Texture** | `Pad` | Multiband three-way split texture with distortion, utility, and chorus | `125-Texture-Tri-Band Split Texture.xpl` |
| 126 | **Vinyl Dust Crackle** | `FX` | Continuous vintage vinyl record dust and crackle generator | `126-Texture-Vinyl Dust Crackle.xpl` |

### 15. FX (9 presets)

*Transitional risers, pitch sweeps, teleport zaps, impacts, and one-shot effects*

| # | Preset Name | Folder | Why (Engine Parameters & Sound Reason) | .xpl File Stem |
|---|---|---|---|---|
| 127 | **Bode Barber Riser** | `FX` | Rising saw and noise build with recursive Bode frequency shifter | `127-FX-Bode Barber Riser.xpl` |
| 128 | **Cosmic Teleport** | `FX` | Ascending pitch sweep dimensional teleport effect | `128-FX-Cosmic Teleport.xpl` |
| 129 | **Detonation Tail** | `FX` | Cinematic impact hit with sub drop and reverberant tail | `129-FX-Detonation Tail.xpl` |
| 130 | **Game Over Zap** | `FX` | Retro 8-bit arcade explosion zap one-shot | `130-FX-Game Over Zap.xpl` |
| 131 | **Metal Gong Strike** | `FX` | Physical resonant metallic gong strike percussion | `131-FX-Metal Gong Strike.xpl` |
| 132 | **Noise Riser** | `FX` | Filtered white noise transitional build-up riser | `132-FX-Noise Riser.xpl` |
| 133 | **Sci-Fi Laser Sweep** | `FX` | Downward pitch-modulated sci-fi laser burst | `133-FX-Sci-Fi Laser Sweep.xpl` |
| 134 | **Sonar Ping** | `FX` | Deep ocean submarine acoustic sonar ping | `134-FX-Sonar Ping.xpl` |
| 135 | **Sub Drop Boom** | `FX` | Subwoofer-testing downward pitch drop boom impact | `135-FX-Sub Drop Boom.xpl` |

---

## Alphabetical Preset Index

| Preset Name | Category Group | # | Origin Folder |
|---|---|---|---|
| **808 Boom** | Bass Sub+808 | 000 | `Bass` |
| **Accordion Reed** | Strings+Brass | 107 | `Keys` |
| **Acid Saw Reso** | Bass Acid | 011 | `Bass` |
| **Acoustic Style Pluck** | Pluck | 086 | `Pluck` |
| **Alien Communication** | Texture | 119 | `FX` |
| **Aux Keyscale Pluck** | Pluck | 087 | `Pluck` |
| **B3 Drawbar Organ** | Keys | 072 | `Keys` |
| **Bell Chime Lead** | Bells+Mallets | 098 | `Lead` |
| **Bell FM Ripple Lead** | Lead Sync+FM | 044 | `Lead` |
| **Bell Mallet Pluck** | Bells+Mallets | 099 | `Pluck` |
| **Bend Formant Growl** | Bass Acid | 012 | `Bass` |
| **Bode Barber Riser** | FX | 127 | `FX` |
| **Bode Shifter Drone** | Texture | 120 | `FX` |
| **Bowed Pressure Strings** | Strings+Brass | 108 | `Pad` |
| **Brass Swell Ensemble** | Strings+Brass | 109 | `Pad` |
| **Breath of Machines** | Texture | 121 | `FX` |
| **Cathedral Choir** | Pad Warm | 050 | `Pad` |
| **Celesta Bells** | Bells+Mallets | 100 | `Keys` |
| **Chime Drop** | Bells+Mallets | 101 | `Pluck` |
| **Chorused Saw Solo** | Lead Synth | 026 | `Lead` |
| **Clavinet Funk** | Keys | 073 | `Keys` |
| **Click Attack Bass** | Bass Synth | 017 | `Bass` |
| **Comb Metal Bass** | Bass Synth | 018 | `Bass` |
| **Comb Reso Swell** | Pad Evolving | 060 | `Pad` |
| **Comb Zither** | Pluck | 088 | `Pluck` |
| **Cosmic Drift Lead** | Lead Synth | 027 | `Lead` |
| **Cosmic Teleport** | FX | 128 | `FX` |
| **Custom Shape Wobble** | Bass Reese | 006 | `Bass` |
| **Dark Cinema Drone** | Texture | 122 | `Pad` |
| **Deep Sea Ambient** | Pad Evolving | 061 | `Pad` |
| **Detonation Tail** | FX | 129 | `FX` |
| **Diffusor Cloud Pad** | Pad Evolving | 062 | `Pad` |
| **Digital Glass Tap** | Pluck | 089 | `Pluck` |
| **Diode Bite Lead** | Lead Synth | 028 | `Lead` |
| **Diode Saturated Bass** | Bass Acid | 013 | `Bass` |
| **Dirty Distortion Bass** | Bass Acid | 014 | `Bass` |
| **Dirty Peak Lead** | Lead Synth | 029 | `Lead` |
| **Dream Grand Piano** | Keys | 074 | `Keys` |
| **Dual Octave Hero** | Lead Synth | 030 | `Lead` |
| **Dyno Electric Piano** | Keys | 075 | `Keys` |
| **Electric Mute Pluck** | Pluck | 090 | `Pluck` |
| **EMS Squelch Bass** | Bass Acid | 015 | `Bass` |
| **Ethereal Glass** | Pad Evolving | 063 | `Pad` |
| **Fast Arp Bass** | Sequence | 114 | `Bass` |
| **Fat Reese** | Bass Reese | 007 | `Bass` |
| **FM Digital Lead** | Lead Sync+FM | 045 | `Lead` |
| **FM Soft Organ** | Keys | 076 | `Keys` |
| **Formant Pop Pluck** | Pluck | 091 | `Pluck` |
| **Formant Vowel Bass** | Bass Synth | 019 | `Bass` |
| **Frozen Aurora** | Pad Evolving | 064 | `Pad` |
| **Game Over Zap** | FX | 130 | `FX` |
| **Gentle Flute Pad** | Pad Warm | 051 | `Pad` |
| **Glass Organ Tones** | Keys | 077 | `Keys` |
| **Golden Dawn Pad** | Pad Warm | 052 | `Pad` |
| **Gritty Square Bass** | Bass Synth | 020 | `Bass` |
| **Harmonic Cloud** | Pad Evolving | 065 | `Pad` |
| **Harmonic Shimmer Lead** | Lead Synth | 031 | `Lead` |
| **Harmonic Sine Bass** | Bass Sub+808 | 001 | `Bass` |
| **Harmonic Tine Pluck** | Pluck | 092 | `Pluck` |
| **Harp Drops** | Pluck | 093 | `Pluck` |
| **Harpsichord Bar** | Strings+Brass | 110 | `Keys` |
| **Hyper Dimension Pad** | Pad Warm | 053 | `Pad` |
| **Hyper Saw Lead** | Lead Synth | 032 | `Lead` |
| **Infinite Horizon** | Pad Evolving | 066 | `Pad` |
| **Just Intonation Keys** | Keys | 078 | `Keys` |
| **Kalimba Tines** | Bells+Mallets | 102 | `Pluck` |
| **Karplus Harp** | Pluck | 094 | `Pluck` |
| **Laser Beam Lead** | Lead Sync+FM | 046 | `Lead` |
| **Lo-Fi Rhodes** | Keys | 079 | `Keys` |
| **Lorenz Garden** | Pad Evolving | 067 | `Pad` |
| **Lush Analog Pad** | Pad Warm | 054 | `Pad` |
| **Marimba Synth** | Bells+Mallets | 103 | `Pluck` |
| **Metal Gong Strike** | FX | 131 | `FX` |
| **Metallic Edge Lead** | Lead Synth | 033 | `Lead` |
| **Mid Side Halo** | Pad Warm | 055 | `Pad` |
| **Moog Ladder Bass** | Bass Synth | 021 | `Bass` |
| **MPE Pressure Lead** | Lead Synth | 034 | `Lead` |
| **Muted Honky Tonk** | Keys | 080 | `Keys` |
| **Noise Riser** | FX | 132 | `FX` |
| **Odd Harmonics Bass** | Bass Synth | 022 | `Bass` |
| **Odd Harmonics Pad** | Pad Warm | 056 | `Pad` |
| **Parallel Dusk Pad** | Pad Evolving | 068 | `Pad` |
| **Pentatonic Step Pluck** | Sequence | 115 | `Pluck` |
| **Phaser Sweep Lead** | Lead Synth | 035 | `Lead` |
| **Pitch Drop 808** | Bass Sub+808 | 002 | `Bass` |
| **Pizzicato Strings** | Strings+Brass | 111 | `Pluck` |
| **Plucked Lead Tone** | Pluck | 095 | `Lead` |
| **Pulse Width Bass** | Bass Synth | 023 | `Bass` |
| **Pulse Width Mod Lead** | Lead Synth | 036 | `Lead` |
| **Punchy Pluck Bass** | Bass Synth | 024 | `Bass` |
| **Radiant Pulse Pad** | Sequence | 116 | `Pad` |
| **Ring Mod Gamelan** | Bells+Mallets | 104 | `Pluck` |
| **Rotary Rock Organ** | Keys | 081 | `Keys` |
| **Rubber Bass Pluck** | Pluck | 096 | `Pluck` |
| **Sci-Fi Laser Sweep** | FX | 133 | `FX` |
| **Scream Filter Bass** | Bass Reese | 008 | `Bass` |
| **Screaming Sync Lead** | Lead Sync+FM | 047 | `Lead` |
| **Shepard Ascent** | Texture | 123 | `Pad` |
| **Slow Motion Waves** | Pad Evolving | 069 | `Pad` |
| **Solid Sub** | Bass Sub+808 | 003 | `Bass` |
| **Solo Square Lead** | Lead Synth | 037 | `Lead` |
| **Sonar Ping** | FX | 134 | `FX` |
| **Space Odyssey Pad** | Pad Evolving | 070 | `Pad` |
| **Space Wind Storm** | Texture | 124 | `FX` |
| **Space Wobble Bass** | Bass Reese | 009 | `Bass` |
| **Spruce Guitar** | Pluck | 097 | `Pluck` |
| **Staccato Bleep Lead** | Lead Synth | 038 | `Lead` |
| **Starlight Shimmer** | Pad Warm | 057 | `Pad` |
| **Stretched Glass Choir** | Pad Evolving | 071 | `Pad` |
| **Sub Drop Boom** | FX | 135 | `FX` |
| **Sub Duo Octave** | Bass Sub+808 | 004 | `Bass` |
| **Subterranean Duo** | Bass Sub+808 | 005 | `Bass` |
| **Subtle Tape Drift** | Pad Warm | 058 | `Pad` |
| **Sync Cry Lead** | Lead Sync+FM | 048 | `Lead` |
| **Sync Growl Bass** | Bass Acid | 016 | `Bass` |
| **Tempo Gate Chords** | Sequence | 117 | `Keys` |
| **Tine Glass Keys** | Keys | 082 | `Keys` |
| **Trance Short Pluck** | Sequence | 118 | `Pluck` |
| **Tri Soft Solo** | Lead Synth | 039 | `Lead` |
| **Tri-Band Split Texture** | Texture | 125 | `Pad` |
| **Triple Fifth Lead** | Lead Synth | 040 | `Lead` |
| **Triple Fold Reese** | Bass Reese | 010 | `Bass` |
| **Twin Warp Lead** | Lead Sync+FM | 049 | `Lead` |
| **Utility Width Keys** | Keys | 083 | `Keys` |
| **Velvet Strings** | Strings+Brass | 112 | `Pad` |
| **Vibraphone Bars** | Bells+Mallets | 105 | `Keys` |
| **Vintage Brass Pad** | Strings+Brass | 113 | `Pad` |
| **Vintage Electric Tine** | Keys | 084 | `Keys` |
| **Vinyl Dust Crackle** | Texture | 126 | `FX` |
| **Vocaloid Lead** | Lead Synth | 041 | `Lead` |
| **Vowel Choir Pad** | Pad Warm | 059 | `Pad` |
| **Vowel Whistle Lead** | Lead Synth | 042 | `Lead` |
| **Wah Pressure Clav** | Keys | 085 | `Keys` |
| **Warm Analog Bass** | Bass Synth | 025 | `Bass` |
| **Warm Toy Piano** | Bells+Mallets | 106 | `Keys` |
| **Warm Vintage Lead** | Lead Synth | 043 | `Lead` |
