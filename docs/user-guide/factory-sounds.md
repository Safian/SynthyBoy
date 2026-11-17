# SynthyBoy Factory Sounds & Preset Guide

This chapter documents the factory sound library for SynthyBoy on Akai MPC standalone hardware and desktop hosts.

The current factory sound library comprises **136 production presets** organized into **15 focused browser groups** in the MPC host Sounds browser (conforming to Akai MPC stock naming conventions, avoiding slashes and punctuation). In addition, **24 new presets** developed for Sound Set Batch 2 (utilizing the new factory content pack of wavetables, samples, multisample instruments, and impulse responses) are detailed in the [Coming with the Next Build](#coming-with-the-next-build-sound-set-batch-2) section.

---

## Browser Groups & Legend

In the MPC Sounds browser and standard preset menus, presets are organized into 15 sub-styles. Each group contains between 5 and 18 presets with consistent sound design and performance characteristics:

1. **Bass Sub+808** (6 presets): Deep pure sine sub-bass, 808 pitch-drop booms, and low-end foundations
2. **Bass Reese** (5 presets): Detuned multi-voice saw basslines, wavefolded reeses, and rhythmic wobbles
3. **Bass Acid** (6 presets): 303-style resonant squelches, diode saturation, distortion, and aggressive growls
4. **Bass Synth** (9 presets): General analog, punchy transient, hollow funk, and pulse-width basslines
5. **Lead Synth** (18 presets): Solo anthems, supersaws, expressive analog, and vocal formant leads
6. **Lead Sync+FM** (6 presets): Hard-sync pitched oscillators, frequency-modulated digital leads, and warp cutters
7. **Pad Warm** (10 presets): Classic analog strings, warm choirs, vintage brass swells, and lush chord beds
8. **Pad Evolving** (12 presets): Slow morphing wavetables, ambient diffusor washes, and shifting spectral clouds
9. **Keys** (14 presets): Acoustic grands, Rhodes/dyno electric pianos, drawbar/rock organs, and clavs
10. **Pluck** (12 presets): Acoustic guitars, Karplus-Strong physical strings, zithers, and transient plucks
11. **Bells+Mallets** (9 presets): Tuned percussion, celestas, vibraphones, marimbas, kalimbas, and struck gamelans
12. **Strings+Brass** (7 presets): Orchestral string ensembles, brass sections, accordions, and baroque harpsichords
13. **Sequence** (5 presets): Self-playing 16-step pentatonic grooves, tempo-synced chord gates, and fast arps
14. **Texture** (8 presets): Continuous atmospheric noise beds, vinyl crackle, drones, and ambient windscapes
15. **FX** (9 presets): Transitional risers, pitch sweeps, teleport zaps, impacts, and one-shot effects

### Table Legend & The `*` Marker

- **Preset Name**: Name as displayed on the MPC touchscreen and in project preset lists.
- **What It Is**: Concise sonic summary and core synthesis architecture (oscillator models, filters, saturation, effects).
- **What to Play**: Practical performance guidance (recommended key register, staccato vs. legato phrasing, chord voicings, velocity sensitivity, and wheel/pressure response).
- **Macro to Try First**: The primary macro control mapped on the MPC hardware Q-Links (or touchscreen) to explore immediately.
- **The `*` Marker**: Presets marked with a trailing `*` designate sounds from the initial 112-preset library whose description and playing guidance are **derived directly from the engine patch parameters, filter models, effect chains, and macro routings**, rather than from subjective listening auditions. Presets without an asterisk (including Batch 1 and Batch 2) were built and measured against offline rendering criteria in the sound lab.

---

## 1. Bass Sub+808

*Deep pure sine sub-bass, 808 pitch-drop booms, and low-end foundations.*

| Preset Name | What It Is | What to Play | Macro to Try First | Status |
|---|---|---|---|:---:|
| **808 Boom \*** | Pitch-gliding pure sine sub bass with classic 808 envelope decay | Single notes C1–G1; strike hard for initial transient knock, hold for booming sub tail | Cutoff (Macro 1) | \* |
| **Harmonic Sine Bass \*** | Smooth sine sub bass enriched with subtle warm upper harmonics | Sustained low roots C1–C2; gentle velocity gives pure sub, higher velocity adds upper presence | Cutoff (Macro 1) | \* |
| **Pitch Drop 808** | Sub 808 with rapid envelope pitch drop and punchy pulse knock through tube drive | Single hits C1–G1; strike hard for punchy knock and tight sub tail | Drop (Macro 1: pitch drop amount and knock intensity) | |
| **Solid Sub \*** | Deep pure sine sub bass foundation designed to anchor electronic mixes | Sustained single notes C1–B1; play cleanly without overlap to prevent phase mud | Cutoff (Macro 1) | \* |
| **Sub Duo Octave \*** | Dual octave stacked pure sine sub bass providing heavy fundamental weight | Deep roots C1–G2; works well for modern hip hop and trap basslines | Cutoff (Macro 1) | \* |
| **Subterranean Duo** | Pure sine sub paired with a snarling, key-folded saw growl layer | Single notes C1–B2 (short or held); velocity 60–127; lean on pad for cutoff bite, wheel for drive | Bite (Macro 1: opens cutoff and growl bite) | |

---

## 2. Bass Reese

*Detuned multi-voice saw basslines, wavefolded reeses, and rhythmic wobbles.*

| Preset Name | What It Is | What to Play | Macro to Try First | Status |
|---|---|---|---|:---:|
| **Custom Shape Wobble \*** | Detuned dual-saw bass driven by a rhythmic hand-drawn three-hop wobble LFO | Held notes C1–C2; let the three-hop rhythmic shape bounce over tempo | Cutoff (Macro 1) | \* |
| **Fat Reese \*** | 5-voice detuned sawtooth Reese bass built for drum & bass and heavy neuro lines | Sustained fifths or single notes C1–C2; adjust mod wheel for filter opening | Cutoff (Macro 1) / Detune (Macro 4) | \* |
| **Scream Filter Bass \*** | 3-voice detuned saw bass driven aggressively through the Scream filter model | Punchy eighth notes C1–G2; higher velocity drives the scream resonance | Cutoff (Macro 1) / Drive (Macro 2) | \* |
| **Space Wobble Bass \*** | Dub-style wobble bass with LFO-modulated lowpass cutoff for rolling basslines | Long held roots C1–F2; tweak LFO rate or wheel to sync with track tempo | Cutoff (Macro 1) / Decay + LFO Rate (Macro 3) | \* |
| **Triple Fold Reese** | 3-oscillator detuned saw Reese with sine-fold wavefolding that snarls on attack then melts smooth | Long fifth chords C1–C2; play firmly to hear initial sine-fold distortion snarl | Fold (Macro 1: sine-fold saturation depth) | |

---

## 3. Bass Acid

*303-style resonant squelches, diode saturation, distortion, and aggressive growls.*

| Preset Name | What It Is | What to Play | Macro to Try First | Status |
|---|---|---|---|:---:|
| **Acid Saw Reso \*** | Classic 303-style resonant sawtooth acid bassline using the Ladder filter model | Fast 16th-note patterns and staccato arps C1–C3 with pitch bends and accents | Cutoff (Macro 1) / Reso (Macro 6) | \* |
| **Bend Formant Growl** | Aggressive talking growl bass using Bend warp through parallel Formant and Analog filters | Staccato eighth notes C1–G2 with wheel moving to shift vowel and drive | Mouth (Macro 1: morphs vocal vowel formant) | |
| **Diode Saturated Bass \*** | Warm saturated diode ladder filter resonant bass with squelchy analog character | Driving synthwave or electro bass patterns C1–G2; use velocity to push filter drive | Cutoff (Macro 1) / Drive (Macro 2) | \* |
| **Dirty Distortion Bass \*** | Overdriven industrial bass using Dirty filter model and integrated waveshaper | Gritty industrial or EBM bass riffs C1–D2; hit keys hard for saturated clipping | Cutoff (Macro 1) / Drive (Macro 2) | \* |
| **EMS Squelch Bass \*** | Resonant EMS ladder bass with short snappy filter envelope for squelchy acid lines | Bouncy staccato lines C1–C3; modulate filter cutoff for dynamic squelch | Cutoff (Macro 1) / Reso (Macro 6) | \* |
| **Sync Growl Bass \*** | Hard-sync modulated aggressive growl bass with cutting harmonic overtones | Heavy dubstep or electro riffs C1–A1; wheel modulates oscillator sync pitch | Cutoff (Macro 1) / Drive (Macro 2) | \* |

---

## 4. Bass Synth

*General analog, punchy transient, hollow funk, and pulse-width basslines.*

| Preset Name | What It Is | What to Play | Macro to Try First | Status |
|---|---|---|---|:---:|
| **Click Attack Bass \*** | Punchy synth bass with transient noise click on attack for instant mix punch | Tight funky 16th-note grooves C1–G2; short release keeps mix clear | Cutoff (Macro 1) / Noise (Macro 8) | \* |
| **Comb Metal Bass \*** | Physical resonant comb filter metallic string bass with plucky acoustic body | Slap-style or staccato bass runs C1–C3; sounds great with expressive velocity | Cutoff (Macro 1) | \* |
| **Formant Vowel Bass \*** | Formant filter sweeping vocal vowel bassline with animated talk-box character | Mid-tempo synth grooves C1–G2; modulate cutoff to cycle vocal vowels | Cutoff (Macro 1) | \* |
| **Gritty Square Bass \*** | Raw edgy lo-fi square wave synth bass with warm harmonic grit | Chiptune, electro, or French-touch basslines C1–C2 | Cutoff (Macro 1) / Drive (Macro 2) | \* |
| **Moog Ladder Bass \*** | Vintage Moog ladder filter round bass with warm, fat analog low-end punch | Soul, funk, or disco basslines C1–C3; smooth legato transitions | Cutoff (Macro 1) | \* |
| **Odd Harmonics Bass \*** | Hollow funk bass driven by odd harmonic series for a woody, round tone | Funky syncopated comping C1–A2; great for walking basslines | Cutoff (Macro 1) | \* |
| **Pulse Width Bass \*** | PWM modulated pulse wave electro bass with animated harmonic movement | Rolling 80s synth-pop or electro basslines C3–G4 (sounds two octaves below the keys); wheel sweeps pulse width | Cutoff (Macro 1) | \* |
| **Punchy Pluck Bass \*** | Tight transient bass with short percussive decay for driving club grooves | Fast staccato bass arps C0–C2 (sounds an octave above the keys); velocity controls envelope bite | Cutoff (Macro 1) | \* |
| **Warm Analog Bass \*** | Classic deep house warm analog lowpass bass with gentle round low-end | Deep house, garage, or neo-soul chords/runs C3–F4 (sounds two octaves below the keys) | Cutoff (Macro 1) | \* |

---

## 5. Lead Synth

*Solo anthems, supersaws, expressive analog, and vocal formant leads.*

| Preset Name | What It Is | What to Play | Macro to Try First | Status |
|---|---|---|---|:---:|
| **Chorused Saw Solo \*** | Bright sawtooth solo lead bathed in lush stereo chorus and reverb | Soaring 80s melodic solos C3–C6; bend pitch with wheel for expressive phrasing | Cutoff + Morph (Macro 1) | \* |
| **Cosmic Drift Lead \*** | Slowly drifting ambient analog solo synth lead with 4-voice unison detune | Slow, floating melodic phrases C3–C5 with lots of reverb space | Cutoff + Morph (Macro 1) | \* |
| **Diode Bite Lead \*** | Sharp biting diode ladder filtered aggressive lead with acidic edge | Fast, cutting synth riffs C3–C6; higher velocity bites harder into the filter | Cutoff + Morph (Macro 1) / Warp (Macro 2) | \* |
| **Dirty Peak Lead \*** | Distorted resonant peak filter industrial synth lead with aggressive presence | Driving cyberpunk or industrial melodies C3–C5 | Cutoff + Morph (Macro 1) / Drive (Macro 3) | \* |
| **Dual Octave Hero \*** | Heroic stacked octave dual sawtooth anthem lead for epic festival melodies | Big anthemic stadium hooks C3–C6; plays both fundamental and octave | Cutoff + Morph (Macro 1) | \* |
| **Harmonic Shimmer Lead \*** | Upper harmonic rich shimmering solo synth lead with chorus and delay | Bright pop and electronic top-lines C4–C6; sustain notes to hear shimmer | Cutoff + Morph (Macro 1) | \* |
| **Hyper Saw Lead \*** | 7-voice detuned dual supersaw stadium anthem lead (14 unison voices total) | Big hands-in-the-air trance and EDM hooks C3–C6; huge stereo field | Cutoff + Morph (Macro 1) / Detune (Macro 3) | \* |
| **MPE Pressure Lead \*** | Expressive MPE solo lead with per-note pressure filter modulation and slide | Expressive legato solos C3–C5; press into pads for filter swells, slide for warp | Cutoff + Morph (Macro 1) | \* |
| **Metallic Edge Lead \*** | Aggressive electro lead with metallic overtone edge and tube distortion | Punchy electro-house and glitch melodies C3–C5 | Cutoff + Morph (Macro 1) | \* |
| **Phaser Sweep Lead \*** | Spacey multi-stage phaser swept solo synth lead with swirling stereo motion | 70s space-rock and prog-synth solos C3–C5 | Cutoff + Morph (Macro 1) | \* |
| **Pulse Width Mod Lead \*** | Classic 80s chorus-widened pulse-width modulated lead synth | Retro-wave synth-pop lead melodies C3–C6; silky smooth top end | Cutoff + Morph (Macro 1) | \* |
| **Solo Square Lead \*** | Expressive monophonic retro square wave solo lead with portamento glide | Monophonic legato leads C3–C6; glide between keys for retro video-game or funk solos | Cutoff + Morph (Macro 1) | \* |
| **Staccato Bleep Lead \*** | Retro arcade-inspired punchy staccato bleep melody lead with fast envelope decay | Fast 16th-note arpeggiated melodic runs C4–C6; bouncy chiptune vibe | Cutoff + Morph (Macro 1) | \* |
| **Tri Soft Solo \*** | Flute-like soft triangle wave solo lead with gentle attack and warm reverb | Lyrical, delicate melodies C4–C6; beautiful for intimate ballads | Cutoff + Morph (Macro 1) | \* |
| **Triple Fifth Lead** | Stacked 3-oscillator power-chord lead with fifth and octave layers through Multi SVF | Single-note heroic riffs C3–C5; the preset automatically forms a fifth chord | Edge (Macro 1: Multi SVF filter edge and drive) | |
| **Vocaloid Lead \*** | Singing formant vocal tract lead synthesizer mimicking human speech vowels | Expressive pop hooks C4–C6; use pitch bend and vibrato wheel | Cutoff + Morph (Macro 1) | \* |
| **Vowel Whistle Lead** | Mono legato vocal formant morph lead with expressive vibrato and whistling resonance | Smooth legato lines C4–C6; sweep wheel to morph vowel, press pad for mouth opening | Mouth (Macro 1: morphs vowel formant coordinates) | |
| **Warm Vintage Lead \*** | Smooth vintage analog lowpass filtered solo lead with chorus warmth | Classic jazz-fusion or synth-pop solos C3–C5; warm and mellow tone | Cutoff + Morph (Macro 1) | \* |

---

## 6. Lead Sync+FM

*Hard-sync pitched oscillators, frequency-modulated digital leads, and warp cutters.*

| Preset Name | What It Is | What to Play | Macro to Try First | Status |
|---|---|---|---|:---:|
| **Bell FM Ripple Lead** | Sine carrier phase-modulated FM bell lead with dynamic velocity bite | Slow, dynamic melodies C4–C6; soft hits ring delicate, hard hits bark like metal | Ripple (Macro 1: FM modulation depth and brightness) | |
| **FM Digital Lead \*** | Crisp digital FM synthesis lead with sharp harmonic modulation | Precise digital melodies C3–C6; cutting attack cuts through dense mixes | Cutoff + Morph (Macro 1) / Warp (Macro 2) | \* |
| **Laser Beam Lead \*** | Futuristic cutting sync and warp modulation laser lead with snappy decay | Rapid laser stabs and sci-fi melodic motifs C4–C6 | Cutoff + Morph (Macro 1) | \* |
| **Screaming Sync Lead \*** | Aggressive pitched oscillator hard-sync rock lead with distortion drive | Hard rock and metal synth solos C3–C5; wheel sweeps hard-sync harmonic overtone | Cutoff + Morph (Macro 1) / Warp (Macro 2) | \* |
| **Sync Cry Lead** | Expressive singing hard-sync saw lead with envelope sweep backed by wide detuned-saw fifth | Expressive legato phrases C3–C5; press into pad for sync sweep, wheel for vibrato | Cry (Macro 1: opens hard-sync pitch sweep depth) | |
| **Twin Warp Lead \*** | Dual sync and bend warp stages in series with LFO sweep for cutting modern leads | Modern EDM or dubstep top-lines C3–C5; animated dual-warp harmonics | Cutoff + Morph (Macro 1) / Warp (Macro 2) | \* |

---

## 7. Pad Warm

*Classic analog strings, warm choirs, vintage brass swells, and lush chord beds.*

| Preset Name | What It Is | What to Play | Macro to Try First | Status |
|---|---|---|---|:---:|
| **Cathedral Choir \*** | Vast reverberant sacred choir pad with vocal formant resonance | Majestic slow sacred chords C2–C4; hold notes to let cathedral reverb bloom | Cutoff + Reso (Macro 1) | \* |
| **Gentle Flute Pad \*** | Soft breathy woodwind flute ensemble pad with subtle noise chiff | Warm pastoral chords C3–C5; soft velocity keeps breath gentle | Cutoff + Reso (Macro 1) | \* |
| **Golden Dawn Pad \*** | Peaceful warm morning dawn analog pad blending sine and triangle warmth | Gentle sunrise chords C2–C4; long release creates seamless transitions | Cutoff + Reso (Macro 1) | \* |
| **Hyper Dimension Pad \*** | Sine-to-saw pad expanded with Hyper/Dimension stereo width effect | Lush cinematic chords C2–C4; immense stereo space | Cutoff + Reso (Macro 1) | \* |
| **Lush Analog Pad \*** | Enveloping classic analog string/synth pad with 12 unison voices and chorus | Warm extended jazz/soul chords C2–C4; velvety rich warmth | Cutoff + Reso (Macro 1) | \* |
| **Mid Side Halo** | 3-oscillator warm pad with Mid/Side processing, solid mono core, and swirling side halo | Lush chords C2–C4 with immense stereo width that never cancels in mono | Halo (Macro 1: side-channel chorus/phaser level) | |
| **Odd Harmonics Pad \*** | Warm hollow wooden pipe organ pad with odd harmonic series | Contemplative church chords C2–C5; woody, organic presence | Cutoff + Reso (Macro 1) | \* |
| **Starlight Shimmer \*** | Bright crystalline shimmering high-register synth pad with airy shimmer | High octave chords C3–C6; sparkling ambient atmospheres | Cutoff + Reso (Macro 1) | \* |
| **Subtle Tape Drift \*** | Vintage warm pad with subtle tape flutter, wow, and pitch drift | Chillhop, lo-fi, and nostalgic synth chords C2–C4 | Cutoff + Reso (Macro 1) / Tape Drift (Macro 3) | \* |
| **Vowel Choir Pad \*** | Synthetic choral pad with warm formant vowel blending and long release | Choral progressions C2–C5; vowels shift smoothly across the register | Cutoff + Reso (Macro 1) | \* |

---

## 8. Pad Evolving

*Slow morphing wavetables, ambient diffusor washes, and shifting spectral clouds.*

| Preset Name | What It Is | What to Play | Macro to Try First | Status |
|---|---|---|---|:---:|
| **Comb Reso Swell \*** | Acoustic-like resonant comb filter swelling wash with string-like bloom | Slow sustained chords C2–C4; allow 2 seconds for comb resonance to swell | Cutoff + Reso (Macro 1) | \* |
| **Deep Sea Ambient \*** | Submerged oceanic atmosphere with slow Ladder filter movement and deep decay | Low rumbling chords C1–C3; deep aquatic textures for film/game scoring | Cutoff + Reso (Macro 1) | \* |
| **Diffusor Cloud Pad \*** | Detuned saw pad smeared by Diffusor filter model into slow drifting clouds | Expansive ambient chords C2–C4; smeared diffusion blurs individual notes | Cutoff + Reso (Macro 1) | \* |
| **Ethereal Glass \*** | Airy crystalline glass texture with slowly shifting overtone partials | Floating ambient chords C3–C5; crystalline highs and long reverb decay | Cutoff + Reso (Macro 1) | \* |
| **Frozen Aurora \*** | Cold crystalline northern lights atmospheric soundscape with delay trails | Icy Nordic chords C2–C5; let the rhythmic delays echo out | Cutoff + Reso (Macro 1) | \* |
| **Harmonic Cloud \*** | Dense evolving harmonic cluster pad with 8-voice unison and gentle modulation | Massive ambient chords C2–C4; hold for 10+ seconds to hear harmonic drift | Cutoff + Reso (Macro 1) | \* |
| **Infinite Horizon \*** | Massive cinematic landscape pad with endless evolving decay and dual wavetables | Slow, dramatic soundtrack chords C1–C4; epic cinematic scale | Cutoff + Reso (Macro 1) | \* |
| **Lorenz Garden** | Generative bell pad driven by chaotic Lorenz and Rössler LFO modulators | Sustained chords C3–C5; let the chaotic, non-repeating modulation evolve | Chaos (Macro 1: Lorenz/Rössler modulation depth) | |
| **Parallel Dusk Pad** | Dual parallel stereo filters (Analog LP left, Comb right) with opposing LFO motion | Slow headphone chords C2–C4 to fully immerse in the opposing stereo breathing | Dusk (Macro 1: parallel filter balance and cutoff) | |
| **Slow Motion Waves \*** | Slow morphing wavetable sweep across spectral frames with sweeping morph | Evolutive ambient chords C2–C4; hold keys to hear wavetable frames traverse | Cutoff + Reso (Macro 1) / Morph (Macro 2) | \* |
| **Space Odyssey Pad \*** | Expansive sci-fi atmospheric pad with cosmic movement, delay, and reverb | Deep space chords C2–C4; majestic cinematic pads for science fiction | Cutoff + Reso (Macro 1) | \* |
| **Stretched Glass Choir** | Spectral-stretched partials drifting across random LFOs over harmonic partials | Slow chords C3–C5, held 4+ seconds; use pad pressure for filter shimmer | Stretch (Macro 1: spectral harmonic stretch factor) | |

---

## 9. Keys

*Acoustic grands, Rhodes/dyno electric pianos, drawbar/rock organs, and clavs.*

| Preset Name | What It Is | What to Play | Macro to Try First | Status |
|---|---|---|---|:---:|
| **B3 Drawbar Organ \*** | Warm tonewheel jazz drawbar organ with fast release and authentic click | Rhythmic jazz comping and blues chords C2–C5 | Cutoff + Warp (Macro 1) / Rotary (Macro 2) | \* |
| **Clavinet Funk \*** | Funky 70s electronic clavinet keyboard with resonant filter snap and bite | Snappy syncopated funk riffs C2–C4; tight staccato key hits | Cutoff + Warp (Macro 1) | \* |
| **Dream Grand Piano \*** | Acoustic grand piano style dynamic keyboard with warm reverb space | Expressive classical or ballad piano progressions C1–C6; dynamic velocity | Cutoff + Warp (Macro 1) | \* |
| **Dyno Electric Piano \*** | Bright modified 80s dyno tine electric piano with bell chime and chorus | 80s ballads, R&B, and fusion chords C2–C5; hard hits ping the tine | Cutoff + Warp (Macro 1) / Chorus (Macro 4) | \* |
| **FM Soft Organ \*** | Smooth gospel tonewheel organ using FM synthesis with sustained body | Gospel and neo-soul chords C2–C5; soft attack and warm sustain | Cutoff + Warp (Macro 1) | \* |
| **Glass Organ Tones \*** | Crystalline cathedral glass church organ with airy spiritual timbre | Hymns and ambient key progressions C2–C5 | Cutoff + Warp (Macro 1) | \* |
| **Just Intonation Keys \*** | Pure harmonic-series keyboard tuned in Just Intonation with A4 at 440 Hz | Pure triad chords C3–C5; hear the completely beatless acoustic harmony | Cutoff + Warp (Macro 1) | \* |
| **Lo-Fi Rhodes \*** | Mellow chillhop Rhodes electric piano with warm tape saturation and chorus | Laid-back neo-soul and lofi hip-hop 7th/9th chords C2–C4 | Cutoff + Warp (Macro 1) | \* |
| **Muted Honky Tonk \*** | Saloon-style upright piano with detuned unison tines and muted attack | Ragtime, blues, and vintage indie piano riffs C2–C5 | Cutoff + Warp (Macro 1) | \* |
| **Rotary Rock Organ \*** | Gritty rock organ through rotary Leslie speaker chorus and tube distortion | Classic rock riffs and driving Hammond-style chords C2–C5; wheel speeds rotary | Cutoff + Warp (Macro 1) / Rotary Speed (Macro 2) | \* |
| **Tine Glass Keys** | Electric piano combining FM sine body with glassy bell tine ping on high velocity | Soulful chords and solos C2–C6; soft velocity gives Rhodes, hard hits bark tine | Tine (Macro 1: FM tine ping brightness and attack level) | |
| **Utility Width Keys \*** | Chorused triangle and square keys with mono low-end utility below 150 Hz | Wide stereo keyboard comping C2–C5; bass stays centered while keys spread wide | Cutoff + Warp (Macro 1) / Width (Macro 4) | \* |
| **Vintage Electric Tine \*** | Classic warm Rhodes electric piano with soft tines and gentle chorus | Soul, R&B, and jazz chords C2–C5; warm and mellow tine response | Cutoff + Warp (Macro 1) | \* |
| **Wah Pressure Clav** | Expressive funk clavinet with pad pressure sweeping Multi SVF wah filter and diode drive | Tight comping chords C2–C4; dig into pads to sweep wah filter and bark drive | Wah (Macro 1: wah filter sweep depth) | |

---

## 10. Pluck

*Acoustic guitars, Karplus-Strong physical strings, zithers, and transient plucks.*

| Preset Name | What It Is | What to Play | Macro to Try First | Status |
|---|---|---|---|:---:|
| **Acoustic Style Pluck \*** | Crisp acoustic guitar transient steel-string pluck with fast decay | Finger-picked folk patterns and fast acoustic arpeggios C2–C5 | Cutoff + Decay (Macro 1) | \* |
| **Aux Keyscale Pluck \*** | Plucked synth with keyboard-scaled dynamic filter sweep deepening up the keyboard | Ascending arpeggios C2–C6; higher keys sweep filter more intensely | Cutoff + Decay (Macro 1) | \* |
| **Comb Zither \*** | Physical modeled plucked zither string using high-resonance comb filter | Asian folk melodies, Celtic zither, or cinematic plucks C3–C6 | Cutoff + Decay (Macro 1) | \* |
| **Digital Glass Tap \*** | Modern UI glassy transient percussive tap tone with short envelope | Fast rhythmic stabs and minimalist electronic patterns C3–C6 | Cutoff + Decay (Macro 1) | \* |
| **Electric Mute Pluck \*** | Palm-muted electric guitar style synth pluck with subtle tube grit | Driving 80s bassline riffs or palm-muted rhythm guitar chugs C2–C4 | Cutoff + Decay (Macro 1) | \* |
| **Formant Pop Pluck \*** | Short staccato vocal formant pop pluck with quirky vowel character | Funky pop staccato lines and quirky ear-candy hooks C3–C5 | Cutoff + Decay (Macro 1) | \* |
| **Harmonic Tine Pluck \*** | Rich harmonic struck tine transient pluck with warm metallic ring | Ambient arpeggios and melodic top-lines C3–C6 | Cutoff + Decay (Macro 1) | \* |
| **Harp Drops \*** | Fluid crystalline concert harp plucked string drops with delay and reverb | Flowing harp glissandos and delicate arpeggios C3–C6 | Cutoff + Decay (Macro 1) | \* |
| **Karplus Harp** | Karplus-Strong physical string model excited by noise burst through tuned comb filter | Sparkling arpeggios C3–C6; wheel palm-mutes string, velocity adds pick bite | Pluck (Macro 1: string decay time and brightness) | |
| **Plucked Lead Tone \*** | Hybrid fast pluck transient with sustained synth body for dance tracks | Progressive house and melodic techno hooks C3–C5; snappy attack, rich body | Cutoff + Decay (Macro 1) | \* |
| **Rubber Bass Pluck \*** | Bouncy rubberized synth pluck with elastic filter response and resonant pop | Bouncy slap-bass rhythms and quirky electro plucks C1–C3 | Cutoff + Decay (Macro 1) | \* |
| **Spruce Guitar \*** | Clean acoustic guitar pluck with warm spruce wood resonance | Acoustic ballad intros and delicate finger-picked patterns C2–C5 | Cutoff + Decay (Macro 1) | \* |

---

## 11. Bells+Mallets

*Tuned percussion, celestas, vibraphones, marimbas, kalimbas, and struck gamelans.*

| Preset Name | What It Is | What to Play | Macro to Try First | Status |
|---|---|---|---|:---:|
| **Bell Chime Lead \*** | Crystalline bell overtones and struck chime lead with delay trails | Bright holiday, ambient, or fantasy top-lines C4–C6 | Cutoff + Morph (Macro 1) | \* |
| **Bell Mallet Pluck \*** | Percussive bell mallet strike with metallic overtones and delay | Gamelan-inspired polyrhythms and tuned percussion riffs C3–C6 | Cutoff + Decay (Macro 1) | \* |
| **Celesta Bells \*** | Fairytale orchestral celesta bell keyboard with sparkling magic character | Delicate music-box and orchestral celesta runs C4–C7 | Cutoff + Warp (Macro 1) | \* |
| **Chime Drop \*** | Delicate falling metallic glass chime cluster with long reverb tail | Slow, sparse melodic notes C4–C6; let each struck chime ring out | Cutoff + Decay (Macro 1) | \* |
| **Kalimba Tines \*** | African wooden thumb piano struck metal tines with dual-octave layer | Calypso, afro-house, and tropical melodic loops C3–C6 | Cutoff + Decay (Macro 1) | \* |
| **Marimba Synth \*** | Warm struck wooden marimba bar resonance with round acoustic body | Fast Latin-jazz runs and minimalist mallet patterns C2–C5 | Cutoff + Decay (Macro 1) | \* |
| **Ring Mod Gamelan** | Inharmonic metallic gamelan bar strike synthesized via ring modulation with a 2.76x carrier | Fast pentatonic patterns C3–C6; hitting harder creates sharper metallic timbre | Metal (Macro 1: ring-mod frequency and shimmer) | |
| **Vibraphone Bars \*** | Warm jazz vibraphone struck metal bars with subtle tremolo and hall space | Jazz comping chords and sustained ballad solos C2–C5; wheel deepens tremolo | Cutoff + Warp (Macro 1) | \* |
| **Warm Toy Piano \*** | Lo-fi indie toy piano with struck bell-like tines and charming imperfection | Quirky indie-pop riffs and nostalgic nursery-style melodies C3–C5 | Cutoff + Warp (Macro 1) | \* |

---

## 12. Strings+Brass

*Orchestral string ensembles, brass sections, accordions, and baroque harpsichords.*

| Preset Name | What It Is | What to Play | Macro to Try First | Status |
|---|---|---|---|:---:|
| **Accordion Reed \*** | Dual reed acoustic folk accordion emulation with slight detuning | Folk, musette, and tango melodies and sustained chord vamps C2–C5 | Cutoff + Warp (Macro 1) | \* |
| **Bowed Pressure Strings** | Expressive bowed string ensemble with pad pressure vibrato and filter dynamics | Chords C2–C5 with Full Level off; press into pads to dig bow into strings | Bow (Macro 1: bow attack bite and filter opening) | |
| **Brass Swell Ensemble** | 3-oscillator swelling brass ensemble with filter dynamics and breath noise | Chords C2–C4; strike harder to accelerate brass bloom and brighten swell | Swell (Macro 1: filter swell rate and brilliance) | |
| **Harpsichord Bar \*** | Crisp baroque plucked harpsichord strings with bright attack and rapid release | Baroque counterpoint, Bach inventions, and crisp classical runs C2–C6 | Cutoff + Warp (Macro 1) | \* |
| **Pizzicato Strings \*** | Orchestral string section short plucked pizzicato with crisp acoustic decay | Bouncy classical string ostinatos and orchestral rhythmic backing C2–C5 | Cutoff + Decay (Macro 1) | \* |
| **Velvet Strings \*** | Silky smooth orchestral synth string section with 6 unison voices and chorus | Cinematic lush string chords C2–C5; gentle attack and release | Cutoff + Reso (Macro 1) | \* |
| **Vintage Brass Pad \*** | Classic 80s warm synth brass ensemble pad with resonant filter swell | Big 80s polysynth brass chords (e.g. Oberheim/Jupiter style) C2–C4 | Cutoff + Reso (Macro 1) | \* |

---

## 13. Sequence

*Self-playing 16-step pentatonic grooves, tempo-synced chord gates, and fast arps.*

| Preset Name | What It Is | What to Play | Macro to Try First | Status |
|---|---|---|---|:---:|
| **Fast Arp Bass \*** | Tempo-synced trance arpeggio bass: a held key plays root, minor third, fifth and octave in sixteenth notes at the host tempo, every step plucked | Hold single root notes C1–C2 (with the transport running) to drive rolling trance and techno basslines | Cutoff (Macro 1) | \* |
| **Pentatonic Step Pluck** | Self-playing 16-step pentatonic arpeggio sequence with synced LFO and per-step plucks | Hold single root notes C3–C4 to let the rhythmic minor pentatonic groove play | Pattern (Macro 1: sequence variations and filter shape) | |
| **Radiant Pulse Pad \*** | Pulsing rhythmic pad with tempo-synced 1.0 Hz chord pulse and chorus | Hold sustained chords C2–C4; the pad rhythmically pulses in sync with the beat | Cutoff + Reso (Macro 1) | \* |
| **Tempo Gate Chords** | Chords rhythmically chopped by a 16-step tempo-synced gate LFO with filter pumping | Hold chords C3–C5 to drive the groove; dial Macro Smooth up to melt into a pad | Gate (Macro 1: gate chop depth vs smooth sustain) | |
| **Trance Short Pluck \*** | Punchy sequenced trance groove pluck with tight decay and stereo delay | Rapid arpeggiator patterns and euro-trance riffs C3–C5 | Cutoff + Decay (Macro 1) | \* |

---

## 14. Texture

*Continuous atmospheric noise beds, vinyl crackle, drones, and ambient windscapes.*

| Preset Name | What It Is | What to Play | Macro to Try First | Status |
|---|---|---|---|:---:|
| **Alien Communication \*** | Resonant formant alien vocal chatter soundscape with fast LFO modulation | Hold single notes C2–C4; strange extraterrestrial vowel chatter emerges | FX Filter (Macro 1) | \* |
| **Bode Shifter Drone \*** | Slow metallic feedback drone through Bode frequency shifter and delay | Hold low notes C1–C3; metallic frequency-shifted feedback wanders endlessly | FX Filter (Macro 1) | \* |
| **Breath of Machines** | Breathing industrial atmosphere with formant noise, tuned combs, and Lorenz LFO | Hold low notes C1–C2 for eerie sci-fi atmospheres and rhythmic machine stutter | Breathe (Macro 1: machine breathing depth and resonance) | |
| **Dark Cinema Drone \*** | Low suspenseful cinematic atmospheric drone with sub-octave layers and reverb | Hold low octave roots C1–C2 for dark thriller, horror, or drama film cues | Cutoff + Reso (Macro 1) | \* |
| **Shepard Ascent** | Endless ascending Shepard tone drone cycling upward via Shepard Tone morph | Hold one or two root notes C2–C3; pressing the pad accelerates the ascent | Ascent (Macro 1: sets the ascending climb rate) | |
| **Space Wind Storm \*** | Howling ambient cosmic wind noise soundscape with sweeping resonant filter | Hold keys C4–C6 to generate stormy cosmic wind atmospheres (sounds two octaves below the keys) | FX Filter (Macro 1) | \* |
| **Tri-Band Split Texture \*** | Multiband three-way split texture (distortion on low, wide utility mid, chorus high) | Hold rich chords C2–C4; each frequency register receives tailored processing | Cutoff + Reso (Macro 1) | \* |
| **Vinyl Dust Crackle \*** | Continuous vintage vinyl record dust, rumble, and crackle generator | Hold any key C2–C4 to inject continuous vintage record hiss and pops under tracks | FX Filter (Macro 1) | \* |

---

## 15. FX

*Transitional risers, pitch sweeps, teleport zaps, impacts, and one-shot effects.*

| Preset Name | What It Is | What to Play | Macro to Try First | Status |
|---|---|---|---|:---:|
| **Bode Barber Riser** | Rising saw and noise build with recursive Bode frequency shifter and flanger | Hold note C2–C4 for 5–6 seconds leading up to a drop; lean on pad for feedback | Rise (Macro 1: pitch climb and riser build intensity) | |
| **Cosmic Teleport \*** | Ascending pitch sweep dimensional teleport effect with fast LFO and delay | Strike single keys C4–C6 for sci-fi dimensional warp transitions (sounds two octaves below the keys) | FX Filter (Macro 1) | \* |
| **Detonation Tail** | Thunderous cinematic impact hit with explosive noise, pitch drop sub, and cavernous tail | Strike single key C3–C4 on downbeats (sounds two octaves below the keys); let the massive sub rumble ring out | Blast (Macro 1: initial explosion punch and sub weight) | |
| **Game Over Zap \*** | Retro 8-bit arcade explosion zap one-shot with waveshaper distortion | Hit single keys C2–C4 for classic arcade game-over explosions and laser zaps | FX Filter (Macro 1) | \* |
| **Metal Gong Strike \*** | Physical resonant metallic gong strike percussion using tuned comb filter | Hit low notes C1–C3; long reverberant metallic decay | FX Filter (Macro 1) | \* |
| **Noise Riser \*** | Filtered white noise transitional build-up riser with sweeping cutoff | Hold key C2–C4 for 4 to 8 bars to build tension before beat drops | FX Filter (Macro 1) | \* |
| **Sci-Fi Laser Sweep \*** | Downward pitch-modulated sci-fi laser burst with snappy envelope | Fast staccato hits C4–C6 for laser gun sound effects | FX Filter (Macro 1) | \* |
| **Sonar Ping \*** | Deep ocean submarine acoustic sonar ping with long reverberant delay | Single isolated strikes C4–C5 spaced several seconds apart | FX Filter (Macro 1) | \* |
| **Sub Drop Boom \*** | Subwoofer-testing downward pitch drop boom impact for club transitions | Trigger C0 on drop downbeats (sounds an octave above the key); massive bass boom rattles subwoofers | FX Filter (Macro 1) | \* |

---

## Coming with the Next Build (Sound Set Batch 2)

The upcoming sound set expansion introduces **24 new factory presets** powered by SynthyBoy's new factory content pack (`content/factory-wavetables` and `content/factory-samples`, synthesized under CC0-1.0 by `scripts/gen-content-pack.py`).

These presets make extensive use of SynthyBoy's non-wavetable voice engines:
- **Sample oscillator** (playback, region looping, 32-slice drum breaks)
- **Granular oscillator** (real-time grain clouds, time-stretching, vowel scrubbing)
- **Spectral oscillator** (additive resynthesis, partial freezes, spectral comb filtering)
- **Multisample oscillator** (SFZ instruments with key zones and velocity switches)
- **Convolve effect** (zero-latency convolution with custom cabinet, plate, and room impulse responses)

Below is the complete catalogue of the 24 new presets, including their **proposed browser group** selected from the fixed 15-group list:

| Preset Name | Proposed Group | Sound Intention & Architecture | What to Play | Macro to Try First | Content Pack Assets Used |
|---|---|---|---|---|---|
| **Reese Dual Phase Bass** | **Bass Reese** | Reese bass synthesized from two opposite breathing saws, 5-voice unison detune, and guitar cabinet convolution | Long roots C1–C3, fifth intervals; wheel controls cutoff and tube drive; pressure opens cutoff | Phase (Macro 1: frame distance between breathing saws) | `pwm_dual_phase.wav`, `cabinet_colour.wav` |
| **Ladder FM Bass** | **Bass Synth** | FM bass stepping through modulator ratios 1 to 8 with pitch-snap envelope into the fundamental | Single notes C1–C3 (short or held); velocity throws initial ratio higher | Ratio (Macro 1: steps through FM integer ratios 1..8) | `fm_ratio_ladder.wav` |
| **Breath Flute Lead** | **Lead Synth** | Solo acoustic-style flute with sine-to-saw tone, authentic breath noise chiff on note attack, and delayed vibrato | Monophonic legato lines C4–C6; non-legato notes trigger breath chiff; pad pressure adds breathiness | Breath (Macro 1: air and blowing noise amount) | `breath_noise_burst.wav` |
| **Spectral Bowed Glass** | **Lead Synth** | Hybrid solo lead combining bowed tone spectral resynthesis through a spectral comb with a glass fundamental table | Mono legato lines C3–C5 with portamento glide; wheel and pressure close the spectral comb | Comb (Macro 1: spectral comb filtering depth) | `bowed_tone.wav`, `additive_glass_harmonica.wav` |
| **Vowel Scrub Choir** | **Lead Synth** | Scrubbable vocal choir lead; granular oscillator reads a 5-second singing vowel sweep driven directly by mod wheel | Melodic lines C3–C5; sweep mod wheel to shift vowels (A at 0, U at 127); pad pressure adds grain scatter | Vowel (Macro 1: manual vocal vowel position) | `voice_sweep_aeiou.wav`, `formant_sweep_aeiou.wav`, `plate.wav` |
| **Vowel Table Lead** | **Lead Synth** | Solo vocal lead with 3-voice unison section driven by a 5-vowel wavetable with formant morphing | Mono legato lines C3–C5 with glide; wheel navigates vowels A-E-I-O-U; pressure adds vibrato | Vowel (Macro 1: wavetable vowel frame) | `formant_sweep_aeiou.wav` |
| **Chord Cloud Pad** | **Pad Evolving** | Granular cloud cut from a 4-chord progression (Cmaj7, Amin7, Fmaj7, G6) with per-voice panning and detuning | Slow chords C2–C4 held for 10+ seconds; slow LFO drifts harmony across chords | Section (Macro 1: starting position in chord progression) | `chord_pad_sections.wav` |
| **Frozen Pluck Drone** | **Pad Evolving** | Granular oscillator parked indefinitely on the first 100 ms of an acoustic pluck, routed to a wet plate convolution bus | Sustained chords C2–C4; wheel moves playhead slightly away from freeze point; pressure adds grains | Freeze Point (Macro 1: selects frozen transient instant) | `pluck_string.wav`, `plate.wav` |
| **Spectral Gong Pad** | **Pad Evolving** | Re-synthesized metal gong partials scanned back and forth like a bowed metal plate, with centered mono bass | Extended chords C2–C4; rich cinematic metal drone; pressure adds 120 Hz inharmonic frequency shift | Scan Speed (Macro 1: spectral scan rate) | `gong_metal.wav`, `additive_bell_strike.wav` |
| **Spectral Vowel Freeze** | **Pad Evolving** | Sung choral vowels frozen in the frequency domain with Perlin LFO morphing without grain beating | Slow sustained chords C2–C4; wheel shifts formants; pad pressure adds upper harmonics | Vowel (Macro 1: morphs vocal spectrum position) | `vowel_pad.wav` |
| **Dusty Rhodes Keys** | **Keys** | FM tine electric piano convolved through an amp cabinet with a dedicated vinyl crackle noise bed | Soul and jazz chords C2–C5 (velocity 50–127); wheel brings in suitcase stereo tremolo | Tine Bark (Macro 1: velocity bark and tine sharpness) | `organ_drawbars_16.wav`, `vinyl_noise_bed.wav`, `cabinet_colour.wav` |
| **Drawbar Organ Keys** | **Keys** | Tonewheel organ stepping through 16 drawbar registrations with key click, 2nd-harmonic percussion, and rotary chorus | Rhythmic organ chords C2–C5; wheel speeds up rotary Leslie speaker modulation | Registration (Macro 1: steps through 16 drawbar registrations) | `organ_drawbars_16.wav` |
| **Glass Zone Bells** | **Bells+Mallets** | Multisample struck wine glasses in 4 key zones, quantized to the Japanese Hirajoshi scale on D with plate convolution | Melodic lines and chords across any keys; automatically conforms to D Hirajoshi scale | Bright (Macro 1: opens 2.8 kHz lowpass filter) | `instruments/glass_bells`, `plate.wav` |
| **Marimba Layer Keys** | **Bells+Mallets** | Acoustic marimba in 5 key zones and 2 velocity layers (soft round bars below 85, hard mallet knock above 85) | Dynamic melodic runs C2–C6; play softly for mellow tones, strike firmly for mallet knock | Zone Shift (Macro 1: shifts zone sample lookups) | `instruments/mallet_marimba`, `small_room.wav` |
| **Glass Ping Pluck** | **Pluck** | Struck wine glass sample tuned across the keyboard with a thin inharmonic bell table and plate convolution | Delicate arpeggios and runs C3–C6; lower keys ring long and dark, high keys ring short and glassy | Dark (Macro 1: closes lowpass filter) | `glass_ping.wav`, `additive_bell_strike.wav`, `plate.wav` |
| **Nylon Zone Pluck** | **Pluck** | Acoustic nylon guitar plucks in 6 key zones with 8-step voice control variation (humanized pan/tuning/filter per note) | Arpeggios and runs C1–C6; wheel doubles strings into detuned unison pair | Bright (Macro 1: high-frequency string bite) | `instruments/pluck_nylon` |
| **Bowed MPE Strings** | **Strings+Brass** | Seamlessly looped bowed string with release bow-lift, rosin noise, and full MPE pad pressure/slide support | Chords C2–C5; on MPE controllers slide opens filter, pressure adds rosin noise and vibrato | Brightness (Macro 1: filter opening) | `bowed_tone.wav` |
| **Breakbeat Slice Keys** | **Sequence** | Authentic 120 BPM drum break sliced into 32 sixteenths mapped across keys C2 to G4, with parallel crushed bus | Play keys C2–G4 as rhythmic drum pads in sequential order; pitch is preserved | Smash (Macro 1: parallel bit-crushed compression bus) | `drum_loop_120bpm.wav`, `small_room.wav` |
| **Stutter Grain Kit** | **Sequence** | Granular drum loop sliced into tempo-synced 1/16th stutters; key pressed selects slice position in the break | Play keys C2–C6 as a glitch sampler; held keys roll into rolling 1/32nd stutters | Nudge (Macro 1: shifts slice playback position) | `drum_loop_120bpm.wav` |
| **Gong Grain Swarm** | **Texture** | Granular swarm shattering metal gong into 60 grains/second with independent pitch, pan, and reverse playback | Held notes C2–C4 for 5+ seconds; wheel introduces secondary delay/reverb echo cloud | Sparse (Macro 1: reduces grain density to quarter) | `gong_metal.wav`, `additive_bell_strike.wav` |
| **Spectral Chord Morph** | **Texture** | 4-chord progression (Cmaj7, Amin7, Fmaj7, G6) re-synthesized as an evolving continuous spectral morph | Hold single key for 8+ seconds; wheel introduces spectral shift warp and frequency shift | Slow (Macro 1: slows morph speed down to 1/5th) | `chord_pad_sections.wav` |
| **Throat Singer Drone** | **Texture** | Overtone throat singing drone with a narrow resonant peak walking 4th to 20th partials in D Phrygian Dominant | Hold 1–2 notes C3–C4 with legato glide; whistling partials sing a melody over the drone | Overtone (Macro 1: overtone harmonic resonance frequency) | `overtone_throat_sweep.wav` |
| **Reverse Gong Swell** | **FX** | Reversed 4-second metal gong decay swelling into a sharp strike, followed by a long plate reverb tail | Single notes C1–C4 held until strike (swell duration scales with pitch: C1 is 8s, C2 is 4s, C3 is 2s) | Slow Down (Macro 1: slows swell to 1/5th speed) | `gong_metal.wav`, `plate.wav` |
| **Rising Grain Riser** | **FX** | 6-second granular tension riser reading climbing noise texture with soaring resonant filter and delay throw | Hold one key (C2 for nominal 6s rise) until peak, release for reverb tail; wheel adds density | Slow (Macro 1: slows build time down to 1/5th) | `rising_texture.wav` |

### Fallback Behavior When Content Pack is Missing

Every preset in Sound Set Batch 2 is engineered to remain musically usable even if the content pack files are uninstalled or missing from the MPC storage:
- **Secondary Wavetable Layer**: Each preset combines its sample, granular, or spectral oscillator with a secondary wavetable layer (tuned between 10% and 30% of oscillator output). If a sample file is absent, the engine bypasses the missing asset cleanly and plays the secondary synth layer (-18 dB to -32 dBFS), ensuring tracks never go completely silent.
- **Wavetable Fallback**: If a factory wavetable WAV is missing, the engine automatically falls back to a clean **sawtooth** waveform.
- **Convolution Fallback**: If an impulse response WAV is missing, the Convolve effect passes the **dry input signal** through without coloration or interruption.
