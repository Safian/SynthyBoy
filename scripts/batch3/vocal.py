# Batch 3, presets built on the Vocal wavetables that no earlier preset used. `add`, `R`, `pressure`, `WT`, `C` are provided by factory-presets-batch3.py.
# The vowel tables are one cycle of a voice at a fixed pitch (the men's tables about 130 to 150 Hz, the women's 220 Hz, the choir 175 Hz), so the formants sit where they should only
# near that pitch (about an octave around it). The four presets set master.transpose to -12: the keys are played an octave above the pitch of the voice, in the range that the
# standard gestures of the loudness method use, and the sound stays inside the range of the table.

add("Soprano Vowel Pad", "Pad", "pad,vocal,soprano,choir,vowel,formant,warm,wavetable,plate",
    "A choir of women: the table walks the vowel triangle (ee, ih, eh, ae, ah, aw, oo) with the average formants of women at 220 Hz, a slow LFO walks the vowels by itself and the wheel by hand, three detuned voices are a small choir, a sine an octave down is the chest; kept an octave below the played key so the formants stay where the table was made",
    {
        "master.polyphony": 6, "master.transpose": -12,
        "osc1.on": 1, "osc1.wavetable": "Preset table", "osc1.frame": 0, "osc1.level": 64, "osc1.unison.voices": 3, "osc1.unison.detune": 8, "osc1.unison.spread": 70,
        "osc2.on": 1, "osc2.wavetable": "Sine", "osc2.level": 18, "osc2.transpose": -12,
        "filter1.on": 1, "filter1.model": "Digital", "filter1.style": 0, "filter1.cutoff": 4800, "filter1.resonance": 10, "filter1.keytrack": 40,
        "env1.attack": 900, "env1.decay": 1500, "env1.sustain": 90, "env1.release": 2200,
        "lfo1.shape": "Sine", "lfo1.rate": 0.1,
        "chorus.on": 1, "chorus.rate": 0.5, "chorus.depth": 40, "chorus.mix": 24,
        "reverb.on": 1, "reverb.type": "Plate", "reverb.decay": 3200, "reverb.mix": 32,
    },
    [
        R("lfo1", "osc1.frame", 40),                                                       # the vowels walk by themselves
        R("mod_wheel", "osc1.frame", 45),
        pressure(36), R("poly_aftertouch", "osc1.frame", 12),
        R("velocity", "filter1.cutoff", 14),
        R("macro1", "osc1.frame", 80),                                                     # Vowel
        R("macro2", "lfo1.rate", 55),                                                      # Vowel Speed
        R("macro3", "osc1.unison.detune", 50),                                             # Choir
        R("macro4", "reverb.mix", 30), R("macro4", "reverb.decay", 20),                    # Space
        R("macro5", "filter1.cutoff", -45),                                                # Dark
        R("macro6", "osc2.level", 45),                                                     # Chest
    ],
    ["Vowel", "Vowel Speed", "Choir", "Space", "Dark", "Chest", "", ""],
    content=C(wavetables=[WT(1, "Vocal/vowel_cycle_women")]))

add("Talk Box Baritone", "Lead", "lead,vocal,talkbox,baritone,vowel,formant,wavetable,mono,legato,wheel",
    "A talk box without the box: the table walks the vowel triangle with the average formants of men at 130 Hz and the wheel is the mouth, from ee to oo; the pressure opens it further, velocity brightens the attack; mono, legato with a glide, a tube for the grit of the amp and tempo echoes; kept an octave below the played key so the formants stay where the table was made",
    {
        "master.polyphony": 1, "master.legato": 1, "master.portamento.time": 55, "master.transpose": -12,
        "osc1.on": 1, "osc1.wavetable": "Preset table", "osc1.frame": 0, "osc1.level": 68, "osc1.unison.voices": 2, "osc1.unison.detune": 5, "osc1.unison.spread": 40,
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 0, "filter1.cutoff": 3600, "filter1.resonance": 15, "filter1.drive": 4, "filter1.keytrack": 45,
        "env1.attack": 20, "env1.decay": 400, "env1.sustain": 92, "env1.release": 220,
        "lfo1.shape": "Sine", "lfo1.rate": 5.0, "lfo1.fade_in": 600,
        "distortion.on": 1, "distortion.type": "Tube", "distortion.drive": 5, "distortion.mix": 40,
        "delay.on": 1, "delay.sync": "Tempo", "delay.division": "1/4 note", "delay.feedback": 28, "delay.mix": 18,
    },
    [
        R("mod_wheel", "osc1.frame", 65),                                                  # the wheel is the mouth
        R("poly_aftertouch", "osc1.frame", 25), pressure(32),
        R("lfo1", "osc1.tune", 5, aux="poly_aftertouch"),                                  # a vibrato that the pressure brings
        R("velocity", "osc1.frame", 12), R("velocity", "filter1.cutoff", 16),
        R("macro1", "osc1.frame", 85),                                                     # Mouth
        R("macro2", "distortion.drive", 50),                                               # Tube Drive
        R("macro3", "filter1.cutoff", -35),                                                # Dark
        R("macro4", "delay.mix", 30),                                                      # Echo
        R("macro5", "lfo1.rate", 40),                                                      # Vibrato Rate
        R("macro6", "osc1.unison.detune", 55),                                             # Double
    ],
    ["Mouth", "Tube Drive", "Dark", "Echo", "Vibrato Rate", "Double", "", ""],
    content=C(wavetables=[WT(1, "Vocal/vowel_cycle_men")]))

add("Choir Swell Ensemble", "Pad", "pad,choir,vocal,ensemble,swell,men-women-children,wavetable,hall",
    "A choir of men, women and children singing ah, aw, oo, eh, ee, ah together, with scattered phases so they do not collapse into one voice: the swell of the envelope walks the vowel a little as the chord opens, three detuned voices and a saw-to-sine octave on top, a slow chorus and a big hall; kept an octave below the played key so the formants stay where the table was made",
    {
        "master.polyphony": 6, "master.transpose": -12,
        "osc1.on": 1, "osc1.wavetable": "Preset table", "osc1.frame": 5, "osc1.level": 62, "osc1.unison.voices": 3, "osc1.unison.detune": 14, "osc1.unison.spread": 80,
        "osc2.on": 1, "osc2.wavetable": "Sine to Saw", "osc2.frame": 30, "osc2.level": 18, "osc2.transpose": 12, "osc2.tune": 3,
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 0, "filter1.cutoff": 4200, "filter1.resonance": 8, "filter1.keytrack": 35,
        "env1.attack": 1600, "env1.decay": 1800, "env1.sustain": 90, "env1.release": 2800,
        "chorus.on": 1, "chorus.rate": 0.6, "chorus.depth": 50, "chorus.mix": 28,
        "reverb.on": 1, "reverb.type": "Hall", "reverb.decay": 4000, "reverb.size": 85, "reverb.mix": 34,
        "eq.on": 1, "eq.low.mode": "High pass", "eq.low.cutoff": 70,
    },
    [
        R("env1", "osc1.frame", 30),                                                       # the vowel opens with the swell
        pressure(36), R("poly_aftertouch", "osc1.frame", 12),
        R("velocity", "filter1.cutoff", 14),
        R("mod_wheel", "osc1.frame", 35), R("mod_wheel", "osc2.level", 20),
        R("macro1", "osc1.frame", 60),                                                     # Vowel Blend
        R("macro2", "env1.attack", 50),                                                    # Attack Time
        R("macro3", "osc2.level", 45),                                                     # High Voices
        R("macro4", "reverb.mix", 35), R("macro4", "reverb.decay", 20),                    # Hall
        R("macro5", "filter1.cutoff", -45),                                                # Dark
        R("macro6", "osc1.unison.detune", 55),                                             # Width
    ],
    ["Vowel Blend", "Attack Time", "High Voices", "Hall", "Dark", "Width", "", ""],
    content=C(wavetables=[WT(1, "Vocal/choir_blend")]))

add("Diphthong Morph Vox", "Lead", "lead,vocal,diphthong,robot,talking,wavetable,steps,mono,legato",
    "A talking synth: the table is eight English diphthongs of eight frames each, every one gliding from its first vowel to its second, with a jump between them; a stepped LFO walks the frame inside every note and the wheel picks the diphthong by hand, so a held key says a word; mono and legato, tempo echoes and a plate; kept an octave below the played key so the formants stay where the table was made",
    {
        "master.polyphony": 1, "master.legato": 1, "master.portamento.time": 35, "master.transpose": -12,
        "osc1.on": 1, "osc1.wavetable": "Preset table", "osc1.frame": 0, "osc1.level": 68, "osc1.unison.voices": 2, "osc1.unison.detune": 6, "osc1.unison.spread": 55,
        "filter1.on": 1, "filter1.model": "Digital", "filter1.style": 0, "filter1.cutoff": 5000, "filter1.resonance": 12, "filter1.keytrack": 45,
        "env1.attack": 12, "env1.decay": 400, "env1.sustain": 92, "env1.release": 220,
        "lfo1.shape": "Steps Up", "lfo1.sync": "Free (Hz)", "lfo1.rate": 1.4,
        "lfo2.shape": "Sine", "lfo2.rate": 5.0, "lfo2.fade_in": 600,
        "delay.on": 1, "delay.sync": "Tempo dotted", "delay.division": "1/8 note", "delay.feedback": 30, "delay.mix": 18,
        "reverb.on": 1, "reverb.type": "Plate", "reverb.decay": 2200, "reverb.mix": 22,
    },
    [
        R("lfo1", "osc1.frame", 50),                                                       # the steps walk the diphthongs inside the note
        R("mod_wheel", "osc1.frame", 40),                                                  # and the wheel picks the word
        R("lfo2", "osc1.tune", 5, aux="poly_aftertouch"),                                  # a vibrato that the pressure brings
        pressure(32), R("poly_aftertouch", "osc1.frame", 12),
        R("velocity", "osc1.frame", 12), R("velocity", "filter1.cutoff", 16),
        R("macro1", "osc1.frame", 85),                                                     # Diphthong
        R("macro2", "lfo1.rate", 50),                                                      # Word Speed
        R("macro3", "filter1.cutoff", -35),                                                # Dark
        R("macro4", "delay.mix", 30), R("macro4", "reverb.mix", 20),                       # Echo Space
        R("macro5", "lfo2.rate", 40),                                                      # Vibrato Rate
        R("macro6", "osc1.unison.detune", 55),                                             # Double
    ],
    ["Diphthong", "Word Speed", "Dark", "Echo Space", "Vibrato Rate", "Double", "", ""],
    content=C(wavetables=[WT(1, "Vocal/diphthong_series")]))
