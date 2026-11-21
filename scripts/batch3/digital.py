# Batch 3, presets built on the Digital wavetables that no earlier preset used. `add`, `R`, `pressure`, `WT`, `C` are provided by factory-presets-batch3.py.

add("FM Chain Tine Keys", "Keys", "keys,fm,electric-piano,tine,wavetable,chorus,plate",
    "An electric piano from a three operator FM chain in one table: a hard strike lands high on the frame axis (bright, bell-like tines) and an envelope lets it fall to a plain, mellow body within half a second, a sine an octave down is the wood, velocity decides how bright the strike is; a slow chorus and a plate finish it",
    {
        "master.polyphony": 10,
        "osc1.on": 1, "osc1.wavetable": "Preset table", "osc1.frame": 10, "osc1.level": 66,
        "osc2.on": 1, "osc2.wavetable": "Sine", "osc2.level": 26, "osc2.transpose": -12,
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 0, "filter1.cutoff": 3200, "filter1.resonance": 10, "filter1.keytrack": 45,
        "env1.attack": 2, "env1.decay": 1800, "env1.sustain": 18, "env1.release": 380,
        "env2.attack": 0, "env2.decay": 450, "env2.sustain": 0,
        "chorus.on": 1, "chorus.rate": 0.8, "chorus.depth": 40, "chorus.mix": 28,
        "reverb.on": 1, "reverb.type": "Plate", "reverb.decay": 1800, "reverb.mix": 20,
        "eq.on": 1, "eq.low.mode": "High pass", "eq.low.cutoff": 60,
    },
    [
        R("env2", "osc1.frame", 40),                                                       # the tine: bright at the strike, mellow after
        R("velocity", "osc1.frame", 28), R("velocity", "filter1.cutoff", 14),
        R("mod_wheel", "osc1.frame", 22), R("mod_wheel", "chorus.mix", 25),
        R("macro1", "osc1.frame", 50),                                                     # Tine Bell
        R("macro2", "env2.decay", 45),                                                     # Tine Decay
        R("macro3", "osc2.level", 40),                                                     # Body
        R("macro4", "chorus.mix", 35), R("macro4", "reverb.mix", 20),                      # Space
        R("macro5", "filter1.cutoff", -30),                                                # Mellow
        R("macro6", "env1.decay", 40),                                                     # Sustain Length
    ],
    ["Tine Bell", "Tine Decay", "Body", "Space", "Mellow", "Sustain Length", "", ""],
    content=C(wavetables=[WT(1, "Digital/fm_chain_123")]))

add("PD Reso Acid Bass", "Bass", "bass,acid,phase-distortion,resonance,mono,legato,diode",
    "An acid line from phase distortion: the table sweeps a resonant peak up the harmonics, a short envelope throws it up and lets it fall at every note so the squelch is in the oscillator, a diode filter and a diode clip bite on top; mono, legato, with a glide, a sine sub under it",
    {
        "master.polyphony": 1, "master.legato": 1, "master.portamento.time": 35,
        "osc1.on": 1, "osc1.wavetable": "Preset table", "osc1.frame": 0, "osc1.level": 66,
        "sub.on": 1, "sub.waveform": "Sine", "sub.octave": -1, "sub.level": 28,
        "filter1.on": 1, "filter1.model": "Diode", "filter1.style": 0, "filter1.cutoff": 1400, "filter1.resonance": 38, "filter1.drive": 8, "filter1.keytrack": 35,
        "env1.attack": 1, "env1.decay": 260, "env1.sustain": 70, "env1.release": 90,
        "env2.attack": 0, "env2.decay": 220, "env2.sustain": 0,
        "distortion.on": 1, "distortion.type": "Diode 1", "distortion.drive": 6, "distortion.mix": 50,
        "delay.on": 1, "delay.sync": "Tempo dotted", "delay.division": "1/8 note", "delay.feedback": 25, "delay.mix": 12,
        "eq.on": 1, "eq.low.mode": "High pass", "eq.low.cutoff": 32,
        "utility.on": 1, "utility.mono_bass": 1, "utility.mono_freq": 140,
    },
    [
        R("env2", "osc1.frame", 55), R("env2", "filter1.cutoff", 20),                      # the squelch at every note
        R("velocity", "osc1.frame", 20), R("velocity", "filter1.cutoff", 16),
        R("mod_wheel", "osc1.frame", 30), R("mod_wheel", "filter1.resonance", 20),
        pressure(30),
        R("macro1", "osc1.frame", 65),                                                     # PD Peak
        R("macro2", "env2.decay", 50),                                                     # Squelch Decay
        R("macro3", "filter1.drive", 40), R("macro3", "distortion.drive", 45),            # Diode Drive
        R("macro4", "filter1.resonance", 55),                                              # Resonance
        R("macro5", "filter1.cutoff", 50),                                                 # Cutoff
        R("macro6", "sub.level", 40),                                                      # Sub
    ],
    ["PD Peak", "Squelch Decay", "Diode Drive", "Resonance", "Cutoff", "Sub", "", ""],
    content=C(wavetables=[WT(1, "Digital/pd_resonance")]))

add("Step Morph Pluck", "Pluck", "pluck,sequence,wave-sequencing,steps,wavetable,echo,rhythmic",
    "A wave sequencing pluck: the table reads eight levels per cycle like a step pattern and morphs through five patterns, a stepped LFO runs the frame inside every note and a random value per key starts each pluck on another pattern, a digital filter and dotted echoes make the line; the first macro is the pattern, the second the speed of the steps",
    {
        "master.polyphony": 6,
        "osc1.on": 1, "osc1.wavetable": "Preset table", "osc1.frame": 0, "osc1.level": 66, "osc1.unison.voices": 2, "osc1.unison.detune": 6, "osc1.unison.spread": 50,
        "filter1.on": 1, "filter1.model": "Digital", "filter1.style": 0, "filter1.cutoff": 1800, "filter1.resonance": 16, "filter1.keytrack": 50,
        "env1.attack": 1, "env1.decay": 380, "env1.sustain": 0, "env1.release": 200,
        "env2.attack": 0, "env2.decay": 260, "env2.sustain": 0,
        "lfo1.shape": "Steps Up", "lfo1.sync": "Free (Hz)", "lfo1.rate": 6.0,
        "delay.on": 1, "delay.sync": "Tempo dotted", "delay.division": "1/8 note", "delay.feedback": 38, "delay.mix": 22,
        "reverb.on": 1, "reverb.type": "Plate", "reverb.decay": 1600, "reverb.mix": 16,
    },
    [
        R("lfo1", "osc1.frame", 40),                                                       # the steps walk the patterns inside every note
        R("note_on_rand_discrete", "osc1.frame", 25),                                      # and every pluck starts on another pattern
        R("env2", "filter1.cutoff", 30),
        R("velocity", "osc1.frame", 20), R("velocity", "filter1.cutoff", 16),
        R("mod_wheel", "osc1.frame", 30), R("mod_wheel", "filter1.cutoff", 20),
        R("macro1", "osc1.frame", 45),                                                     # Pattern
        R("macro2", "lfo1.rate", 60),                                                      # Step Speed
        R("macro3", "filter1.cutoff", -30),                                                # Dark
        R("macro4", "delay.mix", 35),                                                      # Echo
        R("macro5", "env1.decay", 40),                                                     # Length
        R("macro6", "osc1.unison.detune", 50),                                             # Detune
    ],
    ["Pattern", "Step Speed", "Dark", "Echo", "Length", "Detune", "", ""],
    content=C(wavetables=[WT(1, "Digital/step_sequence_morph")]))

add("West Coast Wavefold", "Lead", "lead,fold,west-coast,wavetable,mono,legato,expressive",
    "A west coast lead: a sine that a wave folder drives harder along the frame axis, so the wheel and the pressure turn a pure tone into dense folded metal; an envelope opens the fold at the front of the note and lets it settle, mono and legato with a glide, a little sine fold on top and a basin reverb",
    {
        "master.polyphony": 1, "master.legato": 1, "master.portamento.time": 50,
        "osc1.on": 1, "osc1.wavetable": "Preset table", "osc1.frame": 5, "osc1.level": 66,
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 0, "filter1.cutoff": 6500, "filter1.resonance": 10, "filter1.keytrack": 40,
        "env1.attack": 8, "env1.decay": 500, "env1.sustain": 85, "env1.release": 300,
        "env2.attack": 0, "env2.decay": 500, "env2.sustain": 25,
        "lfo1.shape": "Sine", "lfo1.rate": 5.2, "lfo1.fade_in": 600,
        "distortion.on": 1, "distortion.type": "Sine fold", "distortion.drive": 3, "distortion.mix": 30,
        "reverb.on": 1, "reverb.type": "Basin", "reverb.decay": 2200, "reverb.mix": 18,
    },
    [
        R("env2", "osc1.frame", 30),                                                       # the fold opens at the front of the note
        R("lfo1", "osc1.tune", 4, aux="poly_aftertouch"),                                  # a vibrato that the pressure brings
        R("mod_wheel", "osc1.frame", 50), R("mod_wheel", "distortion.drive", 20),
        pressure(32), R("poly_aftertouch", "osc1.frame", 15),
        R("velocity", "osc1.frame", 16), R("velocity", "filter1.cutoff", 14),
        R("macro1", "osc1.frame", 70),                                                     # Fold Depth
        R("macro2", "distortion.drive", 50),                                               # Fold Drive
        R("macro3", "filter1.cutoff", -40),                                                # Dark
        R("macro4", "reverb.mix", 30), R("macro4", "reverb.decay", 20),                    # Space
        R("macro5", "env2.decay", 45),                                                     # Bite Length
        R("macro6", "lfo1.rate", 40),                                                      # Vibrato Rate
    ],
    ["Fold Depth", "Fold Drive", "Dark", "Space", "Bite Length", "Vibrato Rate", "", ""],
    content=C(wavetables=[WT(1, "Digital/sine_fold_sweep")]))
