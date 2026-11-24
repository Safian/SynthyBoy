# SynthyBoy - the words of docs/release/SOUND-SET-BATCH-1.md: per preset the intention, what to play, and what is not settled for the ear.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Read by scripts/gen-sound-set-batch1-doc.py (the numbers of the document come from docs/release/sound-set-batch1-measurements.json, never from here).

TEXTS = {
"Subterranean Duo": dict(
    intention="A bass for the XL that is clean at the bottom and angry in the middle: a pure sine sub that goes round the filter and a saw growl layer that is folded (key range 36 .. 59 with key fold) so it stays in the two octaves where a growl sounds like one, whatever you play. French LP does the growl, an EQ high pass keeps the saw out of the sub's way, the compressor tightens it.",
    play="Single notes from C1 to B2 (MIDI 24 .. 47), short and held. Velocity 60 .. 127: harder is brighter and angrier. Hold a note and lean on the pad: a little more cutoff. Wheel up: drive and resonance. Macros: Bite (cutoff), Growl (drive and detune), Punch (depth of the filter snap), Sub Weight, Squeeze (compressor).",
    unsure="The balance of the sub and the saw layer (the saw sits about 10 dB under the sub at C2). Whether the key fold is audible as a jump at the edges (an octave jump of the growl layer at C2 / B3 is its definition). French LP's resonance at 38 % is untried by ear.",
),
"Bend Formant Growl": dict(
    intention="The dubstep-style talking bass at a price the XL can pay: one oscillator with the Bend warp swept by a free LFO that the wheel deepens, an analog low pass and the Formant filter model side by side, a second LFO route moving the vowel, diode drive and an EQ clean-up. Mono, so it collapses to mono trivially.",
    play="C1 .. G2, staccato eighth notes with the wheel moving. Velocity moves the vowel and the drive. Macros: Mouth (the vowel), Growl Depth, Wobble Rate (free LFO, 3 Hz up to about 12), Dirt, Sub.",
    unsure="The Formant model's vowel positions (x 30 .. 100) are chosen from the response curves, not by ear. The wobble depth at wheel 0 (about 24 % of the warp range) may be too much or too little.",
),
"Triple Fold Reese": dict(
    intention="The Reese with all three oscillators: a detuned saw pair, a square an octave down, a 24 dB low pass into the Add Bass model, a slow phaser, a mono bottom, and a sine-fold distortion whose drive is held by envelope 3 so the attack snarls and melts into a smooth body. (The fold was first a warp on oscillator 2; that cost 3 times the CPU budget, see the CPU section.)",
    play="Long notes on C1 .. C2, chords of fifths. Macro Fold opens the fold and the filter together (a big change), Drive is the other big one, Phase Drift moves the detune. Wheel: cutoff and fold.",
    unsure="Whether the fold's DC and its upper partials are pleasant in the 2 .. 5 kHz range with Fold at 100 (the hf figure grows by 60 dB). The distortion drive follows the NEWEST note, not each voice (the effect is global): with a chord the fold of the earlier notes follows the last one.",
),
"Pitch Drop 808": dict(
    intention="A kick-drum 808: a sine whose pitch falls from ten semitones above in 110 ms (envelope 3 on the oscillator transpose), a short pulse knock from envelope 4, a tube drive so it speaks on a small speaker, an EQ shelf and a compressor. Mono (polyphony 1).",
    play="Single notes C1 .. G1 (MIDI 24 .. 31), a long note for a long tail. Harder velocity: more pitch fall and a brighter knock. Macros: Drop, Tail, Knock, Grit, Boom.",
    unsure="Whether ten semitones of fall reads as a 'zip' or a kick; the Drop macro takes it to about 14. The click at the start is intended (click ratio 3.4).",
),
"Sync Cry Lead": dict(
    intention="The singing hard-sync lead: one sync oscillator (a 4x warp: it costs what it costs, so one voice of it) whose sync sweep follows velocity, pad pressure and an attack envelope; a three-voice detuned saw a fifth below carries the width instead of unison on the sync oscillator; the wheel brings in a vibrato that fades in.",
    play="C3 .. C5, legato phrases, velocity 90 .. 127. Press the pad harder after the note starts: the sync opens up and the cutoff follows. Wheel for vibrato. Macros: Bright, Cry (sync amount), Thick, Space, Fifth.",
    unsure="The sync range (amount 18 % + envelope + velocity + pressure, up to 100 %) may be too wild at full pressure. The ping-pong delay (eighth notes at the host tempo) is on at 16 %.",
),
"Vowel Whistle Lead": dict(
    intention="A mono legato lead with a human voice: Formant warp and the Formant Scale morph on the oscillator, the Formant filter in series with a Multi SVF peak (style 9: a +9 dB bell). The wheel sweeps the vowel, pressure opens the mouth, a vibrato fades in. Mono, so it is cheap on both devices.",
    play="C4 .. C6, legato lines with a little glide. Wheel slowly from 0 to 127 while holding a note: the vowel travels. Macros: Vowel, Reson, Shape, Air (the position of the peak), Vibrato Rate, Space.",
    unsure="The vowels are positions on the Formant model's x / y plane (35 .. 100 / 45 ..), not tuned to named vowels by ear.",
),
"Triple Fifth Lead": dict(
    intention="A lead that sounds like a stacked band: three oscillators (a power-chord stacked saw, a pulse an octave up, a triangle a twelfth up), one Multi SVF low pass, Hyper/Dimension, a utility width with a mono bottom. At `low` the stack has 3 voices and Hyper/Dimension is bypassed, which is why the Key 61 sounds about 1.5 dB louder and less wide.",
    play="C3 .. C5, single notes, not chords (the stack is the chord). Velocity opens the pulse and the cutoff. Press for the triangle. Macros: Bright, Stack (levels of the upper two), Spread, Space, Edge.",
    unsure="The power-chord stack on the Key 61 (3 unison voices) has not been compared to the XL by ear.",
),
"Bell FM Ripple Lead": dict(
    intention="A sine carrier phase-modulated from a second oscillator (a twelfth up, level 55 with its destination set to None so only the modulation is heard); the index rides envelope 3, velocity and pressure, so a soft note is glassy and a hard one rings like struck metal; Bell Partials add a shimmer.",
    play="C4 .. C6, slow melodies, velocity from 40 to 127 (the difference is the point). Press while a note rings. Macros: Index, Ring (sustain), Shimmer, Space, Ratio (moves the modulator up to 14 semitones: the colour changes, so use it on held notes).",
    unsure="The ratio of 19 semitones is an inharmonic bell: it is meant to be, but the Ratio macro makes it harmonic and then inharmonic again on the way.",
),
"Stretched Glass Choir": dict(
    intention="A pad whose partials are stretched: oscillator 1 (Sine to Saw, Harmonic Stretch morph, its amount wandering on a Perlin random LFO) and a three-voice Harmonic Series layer above it, chorus, a plate reverb, an EQ that thins the lows. One modulated morph only: a morph that moves costs 5 to 9 % of the XL block per oscillator (see the engine notes).",
    play="Chords C3 .. C5, hold 4 s and more; pressure opens the filter and the stretch. Macros: Bright, Stretch, Shimmer, Space, Slow (attack and release: the longest changes).",
    unsure="Harmonic Stretch at 60 .. 100 % moves the character from choir to bell-like; the wandering range (30 % +- 40 %) may be too wide.",
),
"Mid Side Halo": dict(
    intention="Three oscillators and a sub as one warm pad, then split into mid and side: the centre is compressed and EQ'd (it stays dry and solid), the sides get a chorus and a phaser, so the halo grows wide while the middle holds its place in mono. (The first idea, reverb on the side only, did nothing audible because the side content of a pad is small.)",
    play="Wide chords C2 .. C4, slow. Compare mono and stereo: the mono sum loses only about 1 dB. Macros: Brightness, Halo (width and the side effects), Colour, Space, Weight.",
    unsure="Whether the side phaser is too obvious at Halo 100 on headphones.",
),
"Parallel Dusk Pad": dict(
    intention="A slow, dark pad whose image breathes: the two filters run in parallel on opposite halves of the stereo field (an analog 24 dB low pass on the left, a Flanges comb on the right), an LFO opposes their cutoffs and a four second envelope opens the comb. Oscillator 2 keeps its phase per voice (phase memory).",
    play="Slow chords C2 .. C4, best on headphones. Macros: Left Open, Comb Colour, Breath Rate, Space, Upper Layer.",
    unsure="The mono sum is the weakest of the batch (-2.8 dB, worst third octave -6 dB at 1.6 kHz): it is a stereo pad by construction.",
),
"Brass Swell Ensemble": dict(
    intention="A 1980s brass section pad: three detuned saws and a sub, breath noise on the front of the note, a filter that swells with the note and with harder playing, an EQ high pass that keeps the low mids clear.",
    play="Chords C2 .. C4. Hard playing makes the swell faster and brighter. Macros: Bright, Swell Time (attack of the swell and of the level), Breath, Hall, Section (detune).",
    unsure="Whether the swell of 650 ms feels like brass or like strings.",
),
"Tine Glass Keys": dict(
    intention="An electric piano from two sines (the second phase-modulates the first with an index that dies away under envelope 3) plus a bell-partial tine that pings on hard hits; the wheel brings in an auto-pan tremolo, a phaser and a flanger finish it (the flanger is bypassed on the Key 61).",
    play="C2 .. C6, chords and single notes, velocity 40 .. 127: soft is round, hard barks and pings. Wheel: tremolo. Macros: Bell, Bark, Sustain, Phase, Mellow.",
    unsure="The FM bark at velocity 127 (index 12 + 38 + 40 %) may be too hard; the tine at +24 semitones is a high 'ping' that may need to be quieter.",
),
"Karplus Harp": dict(
    intention="A plucked string made of a filter: envelope 4 puts a 16 ms noise burst into the Comb filter model, which is keytracked and calibrated to the note (the cutoff is 261.6256 Hz, the pitch of C4: the comb sounds at its cutoff) and rings for about a second; a second filter damps the highs as it fades; a limiter at the end of the chain catches the pick.",
    play="Arpeggios C3 .. C6, velocity 50 .. 127. The wheel mutes the strings (palm mute). Macros: Brightness, Ring Tone (the comb offset adds the upper partials), Pick Softness (long bursts sound bowed), Space, Body.",
    unsure="Whether the comb's ring is string-like or flute-like; the burst is white noise. Notes above C6 ring for very short times.",
),
"Ring Mod Gamelan": dict(
    intention="Struck bars whose overtones are not harmonic: a triangle through the Ring Mod filter model with a carrier at 2.76 times the note (a bar overtone), a bell-partial ping, a Multi SVF low pass that closes as the sound dies, a dotted-eighth ping-pong delay.",
    play="C3 .. C6 pentatonic patterns. Velocity makes it more metallic. Macros: Metal (the ring-mod mix), Inharmonic (the carrier), Ring Time, Space, Shimmer.",
    unsure="The 2.76 ratio sounds like a marimba bar in theory; the Inharmonic macro moves it (a large change).",
),
"Pentatonic Step Pluck": dict(
    intention="Hold one key and it plays itself: a drawn 16-step LFO (grid 16 x 24) walks the oscillators through the minor pentatonic over one bar and every step is plucked by a second tempo-synced LFO (Exp Down at sixteenths); transpose snap keeps every step in the scale. A limiter at the end catches the plucks.",
    play="One held key at a time, C3 .. C4, at the host tempo (120 BPM when the host does not send one). Wheel: the notes hold between the plucks. Macros: Bright, Hold, Squelch, Echo, Fat. The pattern restarts with every key press.",
    unsure="The pattern (0 7 12 7 3 10 15 12 0 3 7 10 12 15 19 12 semitones) is mine; the owner may want another. A chord plays the pattern in every voice, which is a mush: play single notes.",
),
"Tempo Gate Chords": dict(
    intention="Play a chord and it is chopped by a drawn 16-step gate (values 1, 0.5, 0) that runs with the tempo over one bar, with a quarter-note filter pump (Exp Up LFO on the cutoff) under it; the Smooth macro takes the gate away and leaves a pad.",
    play="Chords C3 .. C5, held. Macros: Smooth, Pump, Bright, Echo, Fat. Wheel: resonance.",
    unsure="The pump is a cutoff pump, not a level pump (a level pump cannot be multiplied with the gate in this engine's matrix); it may be less obvious than a sidechain.",
),
"Shepard Ascent": dict(
    intention="A drone that climbs: two layers an octave apart ride the Shepard Tone morph up an octave in turn under a slow saw LFO (11.8 s), the upper fading out as the lower arrives where it began; a Multi SVF high pass creeps up, a Lorenz random LFO colours it. Polyphony 4: a drone is not played with fists.",
    play="One or two notes C2 .. C3 held for 30 s. Press to speed the climb. Macros: Bright, Climb Speed, Thin, Space, Drift.",
    unsure="THE ASCENT IS NOT PROVEN. Measured: the harmonic emphasis cycles every 11.8 s and the level moves about 3 dB over the cycle; a seamless endless rise is the aim and must be heard.",
),
"Lorenz Garden": dict(
    intention="A slow bed of bell partials and harmonic series where four random LFOs (Lorenz, Rossler, Perlin, sine interpolate) wander over the wavetable frames, the pan and the filters, so no two moments are alike; a Smear filter into an analog low pass, chorus, a dotted ping-pong delay (nothing in it needs the reverb the Key 61 lacks).",
    play="Chords C3 .. C5 held for 10 s and more; Sparkle brings the bell partials up an octave. Macros: Bright, Wander Speed, Sparkle, Echo, Depth.",
    unsure="The Lorenz LFO's slow rate of 0.18 Hz may be too slow to notice inside a short phrase.",
),
"Breath of Machines": dict(
    intention="A machine room that breathes: noise through the Formant filter swept by a Lorenz LFO, then a keytracked Comb filter that gives the hum a pitch, a sample-and-hold stepping the colour, a slow breathing LFO on the noise level.",
    play="One low note C1 .. C2 held for 10 s, or a fifth. Macros: Vowel, Breath Rate, Hum, Room, Stutter.",
    unsure="Whether it is a pleasant atmosphere or an unpleasant one: noise through a resonant comb.",
),
"Bode Barber Riser": dict(
    intention="A five second riser: a saw that climbs two octaves, noise through a rising Multi SVF high pass, and the Bode frequency shifter fed back on itself; it holds at the top until the key is released. Polyphony 4. On the Key 61 the Bode and the flanger are bypassed, the riser still rises (it is 4 dB louder there).",
    play="One note C2 .. C4 held 6 s, then release for the drop. Macros: Height, Spin, Noise, Space, Length. Pressure: the shifter feeds back harder.",
    unsure="Whether the Bode feedback reads as a barber pole on the XL; on the Key 61 it does not exist.",
),
"Detonation Tail": dict(
    intention="A cinematic impact: a noise burst, a sine boom whose pitch collapses, a split at 300 Hz so the low band is saturated while the high band goes into a delay and a big reverb, an EQ and a limiter at the end. Polyphony 4. On the Key 61 the reverb is bypassed (the tail is the delay only).",
    play="One strike C1 .. C2, let it ring 6 s. Macros: Impact, Boom, Tail, Crush, Air.",
    unsure="The limiter holds all velocities near the same level above velocity 80 (80 -> 127: 0.5 dB); the velocity then changes the colour, not the loudness.",
),
"Bowed Pressure Strings": dict(
    intention="Strings you play with the weight of your finger: harder pressure opens the filter, brings in the bow's scratch, a vibrato that was not there (the second LFO route is scaled by the pressure) and a brighter pulse layer; at rest it is a soft, slow-speaking ensemble.",
    play="Chords or lines C2 .. C5 with the pad's Full Level OFF; press harder and softer while holding. Macros: Brightness, Bow Speed (the attack and release), Rosin (bow noise), Vibrato Rate, Hall.",
    unsure="How much pressure range the pad really gives; the layer is tuned for 0 .. 127 over the full travel.",
),
"Wah Pressure Clav": dict(
    intention="A clav with a pedal under the fingertip: pressure sweeps a Multi SVF band-pass wah and tightens its resonance, velocity sets how long the note rings and how hard the diode bites, a pick envelope, an EQ high pass.",
    play="Funk comping C2 .. C4, short chords; press during the chord for the wah; Full Level OFF. Macros: Wah Range (the pressure route's depth: it only acts with pressure), Quack, Pick, Dirt, Phase.",
    unsure="The wah range of 46 % (59 semitones) at full pressure may be too much for a finger; the Wah Range macro scales it.",
),
}


TAIL = """## 7. Engine limits and defects found while building

Everything below was seen in a render or a measurement, with the numbers; nothing was worked around silently (the workaround, where there is one, is named).

1. **Ladder filter (model 2) collapses at the top of the range.** Saw C4, resonance 22, 44.1 kHz, RMS of the output by cutoff: 10 kHz -43 dB, 14 kHz -44, 16 kHz -39, 17 kHz -36, 18 kHz -35, **19 kHz -54, 20 kHz -68**. Dirty, EMS Ladder, French LP, Normal and Digital are flat over the same range. A macro, a pressure route or an envelope that pushes a Ladder cutoff over about 18 kHz makes the sound vanish. Worked around by not using Ladder in this batch (Analog instead).
2. **A source sent to `Direct out` ignores `master.volume`**: the chord level of the sub bass on `Direct out` was +3 dBFS at `master.volume` -12, -30 and -45 alike; only `direct.level` scales it. The levelling tool moves `master.volume` only, so a preset with a Direct-out source cannot be levelled by it. The sub of Subterranean Duo uses `Effects only` (it goes round the filters all the same).
3. **Spectral morph**: *Smear* at 100 % and *High Pass* at 60 % and more give silence (-inf dB); *Random Amplitudes* is not monotonic (on Bell Partials 30 %: -73 dB, 60 %: +6 dB); *Vocode* does nothing without a modulator oscillator; *Phase Disperse* does not change the magnitude spectrum (fine). The Harmonic Stretch, Inharmonic Stretch, Formant Scale and Shepard Tone morphs behave (Shepard Tone at 0 .. 100 % turns the saw into its octave: the odd partials fade out, the even ones stay).
4. **Cost of a moving morph.** Any change of the frame or of the morph amount of an oscillator with a morph type rebuilds that oscillator's morph cycle in every voice: +5 .. 9 % of the XL block per oscillator (Stretched Glass Choir: 14.5 % with three such modulations, 8.2 % with one). The cost is the same whether the amount or the frame moves, and a static morph is free.
5. **The 4x warp costs per unison voice**: one single-voice warped oscillator is +7 .. 9 % of the XL block (Sync 9.6 %, Dist Sine Fold 10.2, Bend 11.6 including the 2.6 % base), and the same oscillator with 3 unison voices three times that (Bend Formant Growl with Bend + Formant warps on 3 voices: 44 %).
6. **Comb filter pitch**: with keytrack 100 the comb sounded 1.1 % flat (about -19 cents; 258.7 Hz for 261.6) at every note: the phase delay of the loop filter was not taken off the delay line. Fixed in the engine (the comb now rings at its cutoff, within 2 cents over C1 .. C6); Karplus Harp's cutoff is the true 261.6256 Hz. Its output is also 30 dB or more under a noise burst's level: a pluck needs +22 dB of make-up after it, and `master.volume` (maximum +6 dB) is not enough, so Karplus Harp uses `eq.level` +12 and `utility.level` +10.
7. **Filter styles**: for most models the twelve styles are identical (Diode, Comb, Ring Mod, Sample Hold, French LP, Add Bass, New Wsp, DJ Mixer, Diffusor, EMS Ladder, PZ SVF, Formant from style 2) and `EQ 6 dB` and `Misc` (styles 0 .. 2) did not change a noise spectrum at cutoff 1 kHz and resonance 30 (they may need `blend` / `var`; not used). DJ Mixer is a low pass below a cutoff of about 600 Hz and a high pass above 800 Hz, not a high pass at any cutoff. The model table of what is a low, high or band pass in which style is in the build log of this work (`scripts/sound-lab.py` renders it).
8. **Effects are global, their modulation follows the newest note**: a voice source (envelope, velocity, LFO) that moves an effect parameter (Triple Fold Reese: `distortion.drive` from envelope 3) is read from the most recently started voice, so with a chord the earlier notes' fold follows the last note's envelope. By design (document 04 section 3.1); it limits what can be called per-note in an effect.
9. **The macro on a route's amount (`modNN.amount` as a destination) works, and `check-presets.py` and `name-preset-macros.py` see a macro through its route's source only**: a macro that acts through the aux source of a route is invisible to them and counts as dead. The new presets avoid it.
10. **`check-presets.py` rule `mod_slot_duplicate`** counts two routes with the same source and destination as duplicates also when their aux sources differ (a vibrato that is always on plus a vibrato that the wheel adds). The engine sums them. Worked around with a second LFO of the same rate.
11. **`scripts/tune-preset-levels.py` holds the chord for 1 s**: a swelling pad (attack 1 .. 3 s) is levelled where it has hardly started and is then 5 .. 10 dB too loud at its sustain (the first hundred's pads have `master.volume` up to +2.9). The new presets are levelled at the sustain (`scripts/sound-lab.py`); the old ones are untouched.
12. **`own_support.cpp` does not list keys the voice implements**: `osc1.unison.stack` (and `stack_repeat`, `blend_shape`), `oscN.map.key_fold`, `oscN.transpose_snap`, `oscN.phase_memory`, `phase_legato`, `map.key_warp`, `fixed_pitch` are read by `core/engine-own/voice/voice.cpp` and work (measured: with the key range 36 .. 59 and key fold on, note 24 sounds at 65.3 Hz and note 72 at 130.9 Hz, without the fold note 24 is silent; transpose snap with the minor pentatonic mask 1193 turns a transpose of 2.49 into 3.00 and of 6.05 into 5.00 semitones; unison stack Power Chord puts the fifth and the octave at -3 and 0 dB against -110 and -6 dB for plain unison), but `implementedKey()` in `core/engine-own/own_support.cpp` says they are not, so `supportsParam` is false for them and `synthyboy_preset_render --engine own` fails the four presets that use them with "needs engine capabilities the engine does not have" (Subterranean Duo, Triple Fifth Lead, Parallel Dusk Pad, Pentatonic Step Pluck: 8 findings). The gate `synthyboy_preset_sanity` runs the Vital engine and does not see it. The fix is the keys in that list; I did not touch the engine. (`sub.mono` is not implemented: removed from the presets.)
13. **Sample, granular, spectral, multisample oscillators and Convolve could not be shown**: the package ships no sample, wavetable or impulse (`content/` has only the presets), and the engine without content gives silence for a sample type and no convolution. Not referenced, as asked.
14. **At `low` the Key 61 loses**: reverb, flanger, Hyper/Dimension, Bode, Convolve (profile `low`). Bode Barber Riser, Detonation Tail, Triple Fifth Lead and Shepard Ascent are the ones that lose most; every preset is levelled on the XL, the Key 61's level is in the table (the largest difference is Bode Barber Riser, +4 dB, because the dry riser is louder than the shifted one).
15. **State version of the LFO shape grid**: `grid_x` / `grid_y` of a drawn LFO shape belong to state version 5 (`core/include/synthyboy/content_state.h`), the factory documents are version 4 (the checker and a pinned test say so). The two presets with a drawn shape carry the keys in a version 4 document: the loader of any version reads them (`core/src/state.cpp`), a version 4 reader skips them. Decide: raise those two documents to 5 and the checker's expectation with them, or keep it.
16. **MPC program files**: `scripts/gen-mpc-xpl.py` takes the program count (113 = Init + 112 presets) from the template file the device wrote; with 136 presets the plugin offers 137 programs, so the template has to be saved again from the new package before `.xpl` files are generated (the tests that need the helper binary or the fixtures were not run here).

## 8. Not shown, and why

- Sample, granular, spectral and multisample oscillators, Convolve: no content in the package (above). A small CC0 pack made by the generator (a few synthesized one-shots, loops and two impulse responses) would open them: that is the first thing for batch 2.
- Filter models: 6 of the 16 the survey lists are in the batch (French LP, Multi SVF in six presets, Ring Mod, Smear, Add Bass, Flanges); Comb and Formant, which the first 112 hardly use, are in two and three. Not used: Slope, Normal, New, DJ Mixer, Multi Morph (the first builds of Subterranean Duo, Triple Fold Reese, Parallel Dusk Pad, Brass Swell Ensemble, Wah Pressure Clav, Shepard Ascent, Bode Barber Riser and Triple Fifth Lead used them; they were replaced because Slope, Normal, New and DJ Mixer cost 3 .. 4 % of the Key 61 block and 6 .. 8 % of the XL's per voice set; the EQ's high-pass mode is the cheap high pass), and Sample Hold, New Wsp, EQ 6 dB, Misc, PZ SVF.
- The oscillator options: key fold and key range (Subterranean Duo), transpose snap (Pentatonic Step Pluck), phase memory (Parallel Dusk Pad): one each; the key-fold and key-range options show up only where the key range of the sound is the point.
- The mixer buses, voice control (`voicectl`), the tuning scales, MPE, the unison stack modes Major / Minor / Harmonic Series: none used.

## 9. Batch 2

1. A listening round on these 24 first: the "Not settled for the ear" lines, the macros that were measured as weak, and the level policy (section 2).
2. A generated CC0 content pack (samples, loops, impulse responses) and the presets that need it: sample / granular / spectral / multisample, convolution; each shown at least twice.
3. The remaining filter models (Sample Hold, New Wsp, EQ 6 dB) and the other warp families (Spectral Comb, Add Harmonics, Spectral Gate, PD, Mask, Vocode with a real modulator), each on a cost that the XL can pay.
4. The buses (bus 1 / bus 2 with their own effect racks, sends), voice control (per-voice pan / detune / cutoff patterns), tuning scales and MPE presets.
5. Per-note effects are not possible; presets for the Key 61 that do not lose a reverb (the plate-like sound built in the oscillator, the delay as the space) as a pair to the XL versions.
6. A pass of the existing 112 against the same measures: the velocity (none of the first hundred changes its tone with velocity: centroid 0.00 oct from 30 to 127) and the pad pressure on the held ones.
"""
