# SynthyBoy - the words of docs/release/SOUND-SET-BATCH-3.md and of the batch 3 table of docs/user-guide/factory-sounds.md (read by scripts/gen-sound-set-batch3-doc.py).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# name: (what it is for, what to play, what differs from docs/release/SOUND-SET-BATCH-3-PLAN.md and why). Plain English, short sentences.

TEXTS = {
    "Analog Sweep Bass": (
        "A punchy analog bass whose filter sweep is in the table.",
        "Roots and fifths C1 to G2. The wheel keeps the sweep open.",
        "The engine filter is open (5200 Hz, the plan said 2400) so the sweep of the table is what is heard. An envelope throws the frame up at every note (the plan had only the macro and the wheel). Six macros, not four. Frame values are percent of the table, not frame numbers."),
    "Trance Stack Lead": (
        "A supersaw lead with the width in the table.",
        "Chords and lines C3 to C5. The wheel opens the spread.",
        "Base frame 30 (the plan said 24, as if it were a frame number). Added Detune and Breath Rate macros. Pad pressure opens the filter (convention of batch 2)."),
    "Sync Sine Screamer": (
        "A mono hard sync lead that screams once at every key and then sings.",
        "Lines C3 to G4, legato with a glide. The wheel pushes the sync ratio.",
        "Added a vibrato that comes in late and the pressure route. The plan's Sub Octave macro is Octave Down. Tube drive and echo as planned."),
    "Sync Saw Raw Bass": (
        "A raw hard sync bass with a firm sub.",
        "Notes C1 to G2, legato slides. The wheel and macro 1 walk the sync ratio.",
        "An envelope also opens the filter a little at the front of the note. Otherwise as planned."),
    "FM Chain Tine Keys": (
        "An electric piano with a bright tine strike that falls to a mellow body.",
        "Chords C2 to G4. Play hard for a brighter strike.",
        "Base cutoff 3200 (the plan said 4200): the tone at the low frames is almost a sine and has no highs to filter. Macro 5 is Mellow (closing), because an opening cutoff did nothing measurable. No pad pressure route (a decaying key, as in batch 2)."),
    "PD Reso Acid Bass": (
        "An acid line whose squelch is in the oscillator.",
        "Mono lines C1 to C3 with legato slides. Strike hard for a higher resonance peak.",
        "Diode filter and diode clip as planned. Macro 3 drives the filter and the distortion together (the filter drive alone was inaudible), macro 4 has a larger range."),
    "Step Morph Pluck": (
        "A wave sequencing pluck: every note steps through the patterns of the table.",
        "Short notes and arpeggios C3 to C5.",
        "The stepped LFO runs free in Hz and restarts at every key, it is not tempo synced as the plan said. Two reasons: the Step Speed macro needs a free rate, and with the free running mode the rate macro had no effect in the offline lab. A random value per key moves the start of every pluck. Base cutoff 1800 so that the Dark macro is audible."),
    "West Coast Wavefold": (
        "A west coast lead: a sine that the frame axis folds into dense metal.",
        "Mono lines C2 to C5. Wheel and pressure fold it harder.",
        "A vibrato that the pressure brings and an envelope on the fold were added. The Basin reverb and the sine fold are as planned (mix 30, drive 3)."),
    "Shimmer Bell Walk": (
        "A bell whose upper partials open with velocity.",
        "Notes and slow chords C3 to G5. Hard keys glitter, soft keys clang.",
        "An envelope on the frame lets the strike fall back to the hum. Added a Shimmer macro. No pad pressure route (a decaying key)."),
    "Free Bar Vibraphone": (
        "Vibraphone bars with a motor tremolo that the wheel switches on.",
        "Melodies and chords F2 to F5. Wheel up is motor on.",
        "The tremolo depth is on the wheel (the plan had it on always). The sub is 14 (plan 18). Chorus is slow, not 3.2 Hz. Macro 1 is named Mallet Hard (the plan said Bar Hardness)."),
    "Gong Bloom Pad": (
        "A pad that opens like a struck gong: dark hum first, bronze after.",
        "Held chords C2 to G4, two seconds or more.",
        "As planned. The amp envelope also drives the frame. Phaser depth 20 and a Dark macro were added."),
    "Cymbal Cloud Riser": (
        "A four second riser from a dull hum to a cymbal cloud, cut at the top.",
        "One note held for five seconds (C3 or any key).",
        "The filter opens with a second envelope. Polyphony 2. The riser is cut at the top (sustain 0), so the natural length is 5.1 s."),
    "Wind Sweep Riser": (
        "A wind riser: a narrow band whistles up through frozen noise.",
        "One key held for five seconds. Every key sounds the same.",
        "Pitch tracking of the oscillator is off and the cycle is fixed at MIDI note 24. At the plan's C3 the band above the 85th harmonic would be above the audio band and the second half of the sweep would be silent. The key is only the trigger."),
    "Air Chiff Flute": (
        "A breathy flute: a triangle with an air layer from the noise table.",
        "Mono lines C3 to C6, legato. Pressure and wheel add breath.",
        "The noise table is a layer that does not follow the keys (fixed pitch, MIDI 36), so the air is the same on every note. The plan's Pressure Sense macro cannot exist (a macro cannot route to a source): replaced by Echo and Room macros."),
    "Pink Noise Sub": (
        "A sub bass with a pitched noise thump that closes within a quarter second.",
        "Notes C1 to G2.",
        "The direction of the plan was wrong: in this table the cutoff falls as the frame goes up, so the note starts low and an envelope with a negative amount (minus 65) starts it bright. Oscillator 48, sub 52 (plan 35 and 55), filter cutoff 3000 (plan 1200) so that the burst is not filtered away. The thump is a tint, not a loud transient (macro 1 moves it by about 1 dB)."),
    "Vinyl Crackle Bed": (
        "A bed of dust, crackle and rattle for the back of a track.",
        "Hold any key. Every key sounds the same.",
        "The cycle is fixed at MIDI note 9 (about 14 Hz), key independent. The table alone is dull (centroid about 700 Hz), so the noise source adds sharp vinyl ticks. A crackle table repeats once per cycle, so the rate is a pitch: this is a dense crackle and rattle bed, not sparse single ticks. Density is a temporal change that the spectral measure does not see."),
    "Acoustic Bore Reed": (
        "A reed solo that moves from clarinet to saxophone with pad pressure.",
        "Mono lines C3 to G5, legato. Press harder for the fuller bore.",
        "The plan's Pressure Sense macro cannot exist: replaced by Dark, Vibrato Rate and Grit. The odd to even change of the table is modest in the measure (3.9 dB), but audible by the thresholds."),
    "Harmonic Wah Bass": (
        "A talking funk bass with a wah in the oscillator.",
        "Lines C1 to C3.",
        "The wah LFO is free in Hz, not tempo synced, so that the Wah Rate macro works. Macro 5 is Dark."),
    "Cathedral Bell Ring": (
        "A bronze cathedral bell that settles from the strike to the hum.",
        "Notes and slow chords C3 to G5. Hard keys ring brighter.",
        "The plan started at frame 0 and added a positive envelope: that would move away from the strike. Here the base frame is 62 and the envelope amount is minus 62, so the note starts at the strike (frame 0) and settles on the hum. Velocity amounts are negative for the same reason."),
    "Dispersion Phase Pad": (
        "A pad whose magnitudes never change and whose waveform does.",
        "Held chords C2 to G4 (they sound an octave lower: master.transpose is minus 12).",
        "The table changes only phases, so the spectral measure sees nothing (macros 1 and 2 and the wheel read as not audible, by construction). Phase is heard only at low pitch and through a nonlinear stage, so a soft clip was added and the pad is kept an octave lower. NOT HEARD: whether the swirl is audible must be judged by ear."),
    "Soprano Vowel Pad": (
        "A choir of women walking the vowels.",
        "Chords A2 to A4 (the table is a voice at 220 Hz; the preset sounds an octave below the key).",
        "master.transpose is minus 12 (the plan had 0), so the standard gestures and the played register sit near the pitch of the table."),
    "Talk Box Baritone": (
        "A talk box lead: the wheel is the mouth.",
        "Mono lines C2 to C4 (an octave below the key, men's voice at 130 Hz).",
        "master.transpose is minus 12. No sub (the plan had one at 32): it would sit below the range of the voice."),
    "Choir Swell Ensemble": (
        "A swelling choir of men, women and children.",
        "Held chords C2 to G4.",
        "Browser group Pad Warm, not Strings+Brass (a voice choir; the existing choirs are in Pad Warm). Second oscillator is Sine to Saw at frame 30 (the plan left the choice open). master.transpose is minus 12."),
    "Diphthong Morph Vox": (
        "A talking synth lead that says a word on a held key.",
        "Mono lines C2 to C4 (an octave below the key).",
        "The stepped LFO is free in Hz and restarts at every key. master.transpose is minus 12. The wheel is read as not audible by the measure because the steps already cover the same frames; it moves the word by hand."),
}
