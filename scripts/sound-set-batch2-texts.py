# SynthyBoy - the words of docs/release/SOUND-SET-BATCH-2.md: per preset what to play and what is not settled for the ear (the intention is the description of the preset itself,
# scripts/batch2/*.py). Read by scripts/gen-sound-set-batch2-doc.py; the numbers of the document come from docs/release/sound-set-batch2-measurements.json, never from here.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

TEXTS = {
"Dusty Rhodes Keys": dict(
    play="Chords and lines C2 .. C5, velocity 50 .. 127: the harder, the more bark in the tine. Wheel up: the suitcase tremolo comes in and the chorus widens. Macros: Tine Bark, Dust (the vinyl loop), Chorus, Dry Amp (0 = the full cabinet), Sustain, Tone. Pad pressure is not used (the sound dies away).",
    unsure="Whether the vinyl bed is too present with five or more notes down: every voice carries its own copy of the loop (random start), so a chord has a chord of crackle (the bed sits at 16 % of the oscillator level). Whether the cabinet at 85 % is the right amount of amp. How the FM table's frame 14 sounds at the top of the keyboard (the table was made for a reading at any pitch, its overtones were measured, not heard).",
),
"Glass Ping Pluck": dict(
    play="Single notes and slow runs C3 .. C6, velocity 40 .. 127. The sample is tuned to C5: below it the glass slows down and darkens (an octave down it rings twice as long), above it it gets short and thin. Wheel: more plate and a wider pair. Macros: Dark (the lowpass closes), Plate, Shimmer (the unison pair beats), Echo, Body (the bell table), Ring.",
    unsure="Whether the whole keyboard sounds like one glass or like a pitched sample (the sample is shifted by up to three octaves: its resonances move with it, which a real glass's would not). The plate impulse response at 38 % is long and bright: how much it blurs fast runs.",
),
"Breakbeat Slice Keys": dict(
    play="Keys C2 (MIDI 36) to G4 (67): thirty-two keys, one sixteenth of the two-bar break each, played in the order of the loop from the lowest key up. A slice plays to the start of the next one, or until the key is up (60 ms release). The pitch does not follow the key (that is how a sliced sample works): the Shift macro moves every key on by up to 20 slices. Macros: Smash (the crushed bus 1 louder and harder), Room (the small-room impulse response), Shift, Dark, Snap (the slice decays to 15 % after 450 ms), Sub Layer (a sine two octaves under the key, which is also what remains if the sample pack is not installed). The break is at 120 BPM and does not follow the host tempo.",
    unsure="Whether bus 1 crushed (bit crush at 80 %, compressor at 70:1) is a useful second copy or too harsh at the Smash macro at 100. At any other tempo than 120 BPM the slices drift against the grid; there is no tempo-sync of the sample in this build of the preset (the scan lock options belong to the scanning types, not to slicing). Slice 31 (the last) is also what every key above G4 plays.",
),
"Bowed MPE Strings": dict(
    play="Chords or lines C2 .. C5, long. Set up for an MPE controller (the MPE zone on, bend range 48): slide opens the filter, pressure brings in the rosin noise and a vibrato; on a keyboard without MPE the wheel does what the slide does and the pad's pressure the rest (Full Level OFF). Release a note to hear the bow lift: the sample plays on past the loop end. Macros: Brightness, Rosin, Vibrato Rate, Bow Speed (attack and release), Hall, Saw Layer.",
    unsure="Whether the loop of the sustain is really smooth by ear: it closes on whole cycles of the vibrato in the file (the loop point mismatch is -60 dB, a 25 ms crossfade is on), but a loop in a sustained tone can show as a periodic swell every two seconds. Whether MPE on does no harm with a plain keyboard: it only widens the bend range of member channels, notes on channel 1 behave as before.",
),
"Reverse Gong Swell": dict(
    play="Single notes C1 .. C4, held until the strike: the length of the swell follows the key (the sample is four seconds long and tuned to C2: C2 swells for 4 s, C3 for 2 s, C1 for 8 s; it plays backwards at the pitch ratio of the key to C2). Let go after the hit for the plate tail. Macros: Slow Down (to a fifth of the speed: a long, nearly still swell), Shimmer (unison detune), Air (noise), Space, Bright.",
    unsure="That the strike at the end of the swell is a cut, not a rise: the reversed gong ends where the original begins, with its 2 ms attack, so the end is a hard edge (the plate and the reverb round it off). Whether notes above C3 are too short to be a swell at all.",
),
"Breath Flute Lead": dict(
    play="Mono, legato: single lines C4 .. C6 with a little glide. Each note that is not played legato starts with a breath chiff (the Sample oscillator): velocity makes it louder; in a legato line there is none, which is how a flute is played. Press the pad for air and a brighter tone. Wheel: more overblown tone and a deeper vibrato. Macros: Breath, Chiff, Vibrato Rate, Bright, Overblow, Space.",
    unsure="The chiff is a band of noise at a fixed pitch (not keytracked): on a very high note it may sound too low. The vibrato is the same LFO for everything, its fade-in is 800 ms.",
),
"Chord Cloud Pad": dict(
    play="Slow chords C2 .. C4, held for ten seconds and more: the play head walks the progression in a 20 second cycle (LFO 0.05 Hz), and the Section macro moves where it starts. The voice control gives six different pan, tuning and brightness settings in turn: play a chord note by note and hear the notes of it stand in different places. Pressure: brighter and more scattered. Wheel: longer grains and a pitch scatter. Macros: Section, Thin Out, Short Grains, Scatter, Bright, Space, Drift.",
    unsure="Whether the grain cloud is a pad or a swarm at the default density (26 a second, 180 ms): the cloud sounds richer with more density, but each grain is another voice of CPU. Whether the six-step voice control lanes are audible as variety or as sloppiness (measured on a plain saw: a lane value of 80 pans 16 dB to one side, 60 on the cutoff moves the centroid by about an octave, 80 on the detune is 0.8 semitone; here pan up to 75, detune up to 12 cents, cutoff up to 24, about half an octave).",
),
"Vowel Scrub Choir": dict(
    play="Single notes or two-note intervals C3 .. C5: the mod wheel is the vowel: at 0 the voice sings A, at 127 U, with E, I, O between, and it holds where you stop (the play head is the wheel). Press the pad for a pitch scatter in the grains, play softer for a darker voice. Macros: Vowel (a second hand on the same axis), Cloud (scatter), Short Grains, Table Layer (the formant table that moves with the wheel), Space, Bright.",
    unsure="That a grain cloud can sing a vowel without a pitch of its own: the grains read a voice at 130 Hz and are pitched by the key, so at 261 Hz the vowel is an octave up and its formants move with it (the table layer is built for 220 Hz and does not shift the same way). At a density of 58 grains a second with 95 ms the grains overlap about five times: the voice is smooth but not as clear as the source.",
),
"Frozen Pluck Drone": dict(
    play="Chords C2 .. C4, held: the plucked string never decays. Wheel: the play head moves a little away from the frozen instant (brighter, rougher) and the plate bus gets louder; pressure: more grains. Macros: Freeze Point (which instant), Cloud, Short Grains, Plate Bus, Pitch Scatter (grains up to six semitones either way), Bright.",
    unsure="That the frozen instant is not a click train: the Gaussian grains of 110 ms at 42 a second overlap about four times, but each grain starts at the same 3 % of the file, so the phase relation between the grains decides how smooth it is (a Perlin LFO and a 3 % offset scatter the start).",
),
"Gong Grain Swarm": dict(
    play="Single notes C2 .. C4, held for five seconds and more (the play head reads the gong in real time and loops). Wheel: more grains and a louder echo cloud (bus 2); pressure: pitch scatter. Macros: Sparse (the grain rate drops to a quarter), Pitch Chaos, Scatter (position, level and direction), Short Grains, Echo Cloud, Dark.",
    unsure="Whether the swarm is a metal texture or noise: the grains are 70 ms of an inharmonic spectrum with pitch scatter up to a fifth, which can sound like a bag of bells; the Nitrous reverb at 6 s on bus 2 may smear it.",
),
"Stutter Grain Kit": dict(
    play="Any key C2 .. C6, played as a sampler: the key chooses where in the two bar break the stutter sits (the first key the start, the highest the end) and transposes it; the grains are sixteenth notes of the host tempo, 1/32 long, so a held key is a rolling stutter on one place. Wheel: scatter of position and pitch (a glitch). Macros: Nudge (the position), Scatter, Snap (the window), Pitch Rand, Dark, Echo.",
    unsure="Tempo: the grain rate follows the host's tempo, the break does not (it is 120 BPM): at 140 BPM the stutter grains are shorter than the break's own sixteenths. Whether the sub layer (a sine two octaves down, there so that something plays if the sample is missing) belongs there.",
),
"Rising Grain Riser": dict(
    play="One key, C2 for the plain build (the sample's root; other keys transpose it and change its length): hold six seconds, let go for the tail. The filter climbs with envelope 2 over 5.6 s. Wheel: louder bus 2 (delay and reverb) and denser grains. Macros: Slow (the read speed down to a fifth), Sparse, Scatter, Throw (bus 2), Edge (resonance), Short Grains.",
    unsure="That the sample's own rise (24 filter slices that climb from 250 Hz to 7 kHz, a stack of 12 harmonics that glides two octaves) and the filter envelope on top of it do not fight: both rise, the sound may get thin at the top. The level is set for the last seconds of the build (it is 10 dB lower at the start).",
),
"Nylon Zone Pluck": dict(
    play="Single notes and arpeggios C1 .. C6. Six recordings of a nylon pluck, one per zone: the zone borders are at E2/F2, C#3/D3, A#3/B3, G4/G#4 and E5/F5 (MIDI 40/41, 49/50, 58/59, 67/68, 76/77; see the SFZ file). Play the same note again and again: the voice control gives every note of eight its own pan, tuning and cutoff, so repeated notes are never the same. Wheel: the strings are doubled (two unison voices that drift apart) and the echo comes up. Macros: Bright, Saw Pluck, Double, Sustain, Echo, Room.",
    unsure="The zone seams: each zone is one recording shifted by up to four semitones either way, so a seam can be heard as a step in the timbre. Whether the saw pluck under it (it is what plays if the pack is missing) should be there at all at its 16 %.",
),
"Marimba Layer Keys": dict(
    play="Lines and chords C2 .. C6. Velocity below 85 plays the soft recording of the bar, above it the hard one with the knock in front: play the same note at 40 and 120 to hear the change. Wheel: tremolo and more of the octave sine. Macros: Zone Shift, Room, Octave Sine, Mellow, Ring, Chorus.",
    unsure="That the velocity switch at 85 is a step: there is no crossfade between the layers (a crossfade needs xfin / xfout in the SFZ). The wheel's effect on the zone lookup is large at the ends of the keyboard and nil in the middle.",
),
"Glass Zone Bells": dict(
    play="Any keys: every note is moved to the nearest note of the Hirajoshi scale on D (D, E, F, A, A#), so a hand can lay any chord on the keys. Single notes and slow chords C2 .. C6. Wheel: the zone lookup shifts and the plate gets louder. Macros: Bright (a lowpass at 2.8 kHz opens), Plate, Bell Layer, Echo, Ring, Sub.",
    unsure="Whether Conform is what you want from a keyboard: with the scale on, the black keys of the chromatic scale are not available (key 'mark only' would show the scale on the pads without moving notes: `master.scale.mode` Mark only). The four zones of glass at C3, C4 and C5 are one recording each, stretched up to seven semitones.",
),
"Spectral Vowel Freeze": dict(
    play="Slow chords C2 .. C4, held for ten seconds and more. A Perlin LFO walks the play head along the voice pad, so the vowel changes by itself; the wheel is a second hand on the same axis (and a frequency shift). Pressure: more added harmonics and a brighter filter. Macros: Vowel, Harmonics, Shift (the frequencies of the whole spectrum up by up to 240 Hz), Hi Cut, Space, Saw Layer.",
    unsure="The spectral oscillator is the dearest voice type: polyphony 4 with two unison voices each; its CPU is the first thing to check on the device. A frequency shift (not a pitch shift) makes the spectrum inharmonic: 5 Hz sounds like a slow phasing, 100 Hz and more is metallic.",
),
"Spectral Bowed Glass": dict(
    play="Mono, legato, C3 .. C5 with some glide. Wheel and pressure close the spectral comb on the bowed tone (the harmonics at every n-th multiple are favoured): hollower, more glass-like. Macros: Comb, Glass (the table layer), Glass Shape, Bright, Vibrato Rate, Space.",
    unsure="Whether the comb sounds like glass or like a phaser: it is a comb in the spectrum of the file, so the notes of the line move it relative to the harmonics. Spectral oscillator CPU: polyphony 1, unison 1.",
),
"Spectral Gong Pad": dict(
    play="Chords C2 .. C4, held long; with a big hall. The gong spectrum is read back and forth (loop forward-reverse between 8 and 55 % of the file) at a quarter of the speed. Pressure: a frequency shift of 120 Hz (inharmonic). Macros: Scan Speed (up to four times), Shift, Subharmonics (the spectral warp that adds partials an octave and more below), Bright, Space, Bell Layer.",
    unsure="That a re-synthesised gong is a pad at all: the partials are inharmonic and keytracked by the shift of the whole spectrum, so a chord of three notes is a cluster of inharmonic partials; this is a drone for film, not for songs. The level (the measurement is made at the sustain, after an attack of 1.2 s).",
),
"Spectral Chord Morph": dict(
    play="One key, held 8 seconds or longer: the file plays once in real time, C major 7, A minor 7, F major 7, G 6 in turn (2 s each), and the oscillator re-synthesises them from their spectrum, so the changes are smooth morphs; other keys transpose all of it. Wheel: a spectral shift warp and a frequency shift (the harmony turns strange). Macros: Slow (to a fifth of the speed), Shift Warp, Freq Shift, Hi Cut, Space, Bright.",
    unsure="Whether the loop at the end of the file (forward loop over the whole file) is a jump: the file's last chord (G 6) is faded out and the first one faded in, so the loop is a dip. The wheel changes the level a great deal with the warp at 60 % (see the measured table).",
),
"Reese Dual Phase Bass": dict(
    play="Long notes on C1 .. C3 (MIDI 24 .. 48), chords of fifths; velocity: harder is brighter and dirtier. Wheel: cutoff and drive. Pressure: the cutoff. Macros: Phase (the table's frame: the distance between the two saws), Cutoff, Growl (tube drive), Dry Cab (the cabinet convolved at 55 %: 0 is the full cabinet), Octave Saw, Movement Rate.",
    unsure="The cabinet impulse response on a bass: it is a guitar-amp colour (hump at 2.5 kHz, 80 Hz roll-off); on a bass it may thin the low mid. On the Key 61 (`low`) Convolve is bypassed: the sound is 6 dB quieter and without the cabinet.",
),
"Vowel Table Lead": dict(
    play="Mono, legato, C3 .. C5 with glide. The wheel walks the five vowels A, E, I, O, U on the frame axis (at 127 it is U); pressure opens the sound and brings in the vibrato; velocity brightens the attack. Macros: Vowel, Dark, Choir (the unison spread), Octave Down, Vibrato Rate, Space.",
    unsure="The table's formants are fixed in Hz for a voice at 220 Hz: on a note an octave higher the same frame has the formants between the harmonics, an octave lower they sit on harmonics of a different number; the best range is A2 .. A4 and above it the vowels turn into bright tones.",
),
"Drawbar Organ Keys": dict(
    play="Chords C2 .. C5. The Registration macro walks the sixteen drawbar registrations (888000000, 888800000, 008800000, 800000008 ... jazz, gospel, full, whistle ...). A key click and a fast second-harmonic percussion sit on every note; the wheel is the rotary's vibrato and brings in more chorus and phaser. Macros: Registration, Percussion, Overdrive (bus 1), Click, Rotary Speed, Bright.",
    unsure="That the 16 foot fundamental in the cycle with the oscillator set an octave up (transpose +12) is what a tonewheel organ's bottom is: it is the same pitch, but the oscillator's transpose and the table's lowest octave mean that the keys below C2 sound at the lowest frame's pitch minus one octave. The Leslie is a chorus and a phaser at the same 0.8 Hz: there is no amplitude modulation.",
),
"Throat Singer Drone": dict(
    play="One or two notes C3 .. C4 (they sound an octave lower), held; legato with a long glide (120 ms). The key is moved to the Phrygian dominant scale on D (D, E flat, F sharp, G, A, B flat, C; measured: C sharp plays C, E plays E flat, G sharp plays G). A slow LFO (0.07 Hz) and a Perlin LFO walk the resonance up and down the overtones, so a melody of whistling partials sings over the drone; the wheel speeds the walk up. Macros: Overtone, Walk Speed, Drone (the sub sine two octaves down), Bright, Space, Choir.",
    unsure="The level (measured at the sustain; an attack of 1.5 s) and whether the overtone resonance is audible as a melody and not just a filter sweep: the narrow resonance (one partial wide) is chosen for it, measured as a 3.7 dB spectral distance between the two ends of the macro at one note.",
),
"Ladder FM Bass": dict(
    play="Single notes C1 .. C3 (MIDI 24 .. 48), short and held. Velocity decides how far the ratio is thrown at the front of the note (a hard key starts on a higher ratio of the FM ladder, with the index rising; it settles into the fundamental in 260 ms). Wheel: the ratio and the drive. Macros: Ratio (the frame: steps through the ratios 1 .. 8, eight frames each), Snap Length, Drive, Sub, Cutoff, Decay.",
    unsure="That the frame steps through the ratios are audible as steps and not as one sound getting brighter: each step is a different set of sidebands, the index rising inside it; with the cutoff at 1.5 kHz at C1 the higher ratios are hidden by the filter (open it with Cutoff).",
),
}
