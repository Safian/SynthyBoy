# Batch 1, leads. `add`, `R` and `pressure` are provided by factory-presets-batch1.py.

add("Sync Cry Lead", "Lead", "sync,warp,pressure,vibrato,expressive",
    "A hard-sync saw whose sync sweep follows the key's velocity, the pad pressure and a per-note envelope; a second saw a fifth below fattens it, the wheel brings in a vibrato that fades in",
    {
        "master.polyphony": 8,
        "osc1.on": 1, "osc1.wavetable": "Saw", "osc1.level": 62,
        "osc1.warp.type": "Sync", "osc1.warp.amount": 18,
        "osc2.on": 1, "osc2.wavetable": "Saw", "osc2.level": 38, "osc2.transpose": -5, "osc2.tune": 6, "osc2.unison.voices": 3, "osc2.unison.detune": 14, "osc2.unison.spread": 70,
        "filter1.on": 1, "filter1.model": "Analog", "filter1.cutoff": 3200, "filter1.resonance": 22, "filter1.keytrack": 50,
        "env1.attack": 6, "env1.decay": 800, "env1.sustain": 85, "env1.release": 260,
        "env3.attack": 20, "env3.decay": 700, "env3.sustain": 25, "env3.release": 300,
        "lfo1.shape": "Sine", "lfo1.rate": 5.4, "lfo1.fade_in": 600, "lfo1.delay": 120,
        "delay.on": 1, "delay.mix": 16, "delay.feedback": 35, "delay.sync": "Tempo", "delay.division": 5, "delay.style": "Ping pong",
        "reverb.on": 1, "reverb.mix": 14, "reverb.decay": 1800, "reverb.type": "Plate",
        "eq.on": 1, "eq.mid.cutoff": 700, "eq.mid.gain": -2, "eq.high.cutoff": 6000, "eq.high.gain": 2,
        "utility.on": 1, "utility.hpf": 30,
    },
    [
        R("env3", "osc1.warp.amount", 30),                                  # the cry of the attack
        R("velocity", "osc1.warp.amount", 24), R("velocity", "filter1.cutoff", 16),
        R("poly_aftertouch", "osc1.warp.amount", 38),                       # pressure leans into the sync
        pressure(26),
        R("lfo1", "osc1.tune", 18, aux="mod_wheel"), R("lfo1", "osc2.tune", 18, aux="mod_wheel"), R("lfo1", "filter1.cutoff", 6, aux="mod_wheel"),   # the wheel: vibrato
        R("mod_wheel", "osc1.warp.amount", 10),
        R("macro1", "filter1.cutoff", 40),                                  # Bright
        R("macro2", "osc1.warp.amount", 40),                                # Cry
        R("macro3", "osc2.unison.detune", 80), R("macro3", "osc2.tune", 12), R("macro3", "osc2.level", 20),   # Thick
        R("macro4", "delay.mix", 40), R("macro4", "reverb.mix", 30),        # Space
        R("macro5", "osc2.level", 35),                                      # Fifth
    ],
    ["Bright", "Cry", "Thick", "Space", "Fifth", "", "", ""])

add("Vowel Whistle Lead", "Lead", "formant,vowel,legato,morph,mono",
    "A mono legato lead with formant warp and Formant Scale morph, a formant filter in series with a Multi SVF peak: the mod wheel sweeps the vowel, pressure opens the mouth",
    {
        "master.polyphony": 1, "master.legato": 1, "master.portamento.time": 90, "master.portamento.slope": 1,
        "osc1.on": 1, "osc1.wavetable": "Saw", "osc1.level": 65,
        "osc1.warp.type": "Formant", "osc1.warp.amount": 25,
        "osc1.morph.type": "Formant Scale", "osc1.morph.amount": 35,
        "osc2.on": 1, "osc2.wavetable": "Triangle", "osc2.level": 40, "osc2.transpose": 12,
        "filter.routing": "Series 1 to 2",
        "filter1.on": 1, "filter1.model": "Formant", "filter1.cutoff": 1800, "filter1.resonance": 55, "filter1.formant.x": 35, "filter1.formant.y": 45, "filter1.formant.resonance": 80, "filter1.keytrack": 40,
        "filter2.on": 1, "filter2.model": "Multi SVF", "filter2.style": 9, "filter2.cutoff": 2600, "filter2.resonance": 45,
        "env1.attack": 20, "env1.decay": 500, "env1.sustain": 90, "env1.release": 220,
        "lfo1.shape": "Sine", "lfo1.rate": 5.0, "lfo1.fade_in": 700, "lfo1.delay": 200, "lfo2.shape": "Sine", "lfo2.rate": 5.0, "lfo2.fade_in": 700, "lfo2.delay": 200,
        "rlfo1.style": "Perlin", "rlfo1.rate": 0.6,
        "chorus.on": 1, "chorus.mix": 22, "delay.on": 1, "delay.mix": 14, "delay.feedback": 30, "delay.sync": "Tempo", "delay.division": 5,
        "reverb.on": 1, "reverb.mix": 12, "reverb.decay": 1600,
    },
    [
        R("mod_wheel", "filter1.formant.x", 55), R("mod_wheel", "osc1.warp.amount", 20),    # the vowel
        R("poly_aftertouch", "filter1.formant.y", 40), pressure(18),                        # pressure: the mouth opens
        R("lfo1", "osc1.tune", 6), R("lfo1", "osc2.tune", 6), R("lfo2", "osc1.tune", 14, aux="mod_wheel"), R("lfo2", "osc2.tune", 14, aux="mod_wheel"), R("lfo1", "filter1.formant.x", 4),
        R("rlfo1", "filter1.formant.x", 6), R("rlfo1", "filter2.cutoff", 8),
        R("velocity", "filter1.cutoff", 14), R("velocity", "osc1.morph.amount", 20), R("velocity", "filter1.formant.y", 30), R("velocity", "osc1.warp.amount", 20),
        R("macro1", "filter1.formant.x", 50),                                               # Vowel
        R("macro2", "filter1.formant.resonance", 30), R("macro2", "filter1.resonance", 55), R("macro2", "filter2.resonance", 30), # Reson
        R("macro3", "osc1.morph.amount", 50),                                               # Shape
        R("macro4", "filter2.cutoff", 35),                                                  # Air
        R("macro5", "lfo1.rate", 35), R("macro5", "lfo2.rate", 35),                                # Vibrato
        R("macro6", "reverb.mix", 30), R("macro6", "delay.mix", 25),                        # Space
    ],
    ["Vowel", "Reson", "Shape", "Air", "Vibrato Rate", "Space", "", ""])

add("Triple Fifth Lead", "Lead", "three-oscillators,power-chord,unison-stack,octaves,hyper,multi-svf",
    "Three oscillators on purpose: a power-chord stacked saw, a pulse an octave up and a fifth-up triangle, run through a Multi SVF low pass, Hyper/Dimension and a utility width: one note sounds like a stack of a band",
    {
        "osc1.on": 1, "osc1.wavetable": "Saw", "osc1.level": 55, "osc1.unison.voices": 5, "osc1.unison.stack": "Power Chord", "osc1.unison.detune": 16, "osc1.unison.spread": 80,
        "osc2.on": 1, "osc2.wavetable": "Pulse 25%", "osc2.level": 30, "osc2.transpose": 12, "osc2.tune": -7,
        "osc3.on": 1, "osc3.wavetable": "Triangle", "osc3.level": 34, "osc3.transpose": 19,
        "master.polyphony": 8,
        "filter1.on": 1, "filter1.model": "Multi SVF", "filter1.style": 2, "filter1.cutoff": 1200, "filter1.resonance": 25, "filter1.keytrack": 55,
        "env1.attack": 8, "env1.decay": 900, "env1.sustain": 80, "env1.release": 320,
        "env2.attack": 10, "env2.decay": 400, "env2.sustain": 30,
        "hyperdim.on": 1, "hyperdim.unison": 4, "hyperdim.detune": 40, "hyperdim.mix": 25, "hyperdim.dim_size": 55, "hyperdim.dim_mix": 20,
        "chorus.on": 1, "chorus.mix": 18,
        "utility.on": 1, "utility.width": 150, "utility.mono_bass": 1, "utility.mono_freq": 180,
        "delay.on": 1, "delay.mix": 14, "delay.sync": "Tempo", "delay.division": 5, "delay.feedback": 32,
    },
    [
        R("env2", "filter1.cutoff", 18),
        R("velocity", "filter1.cutoff", 20), R("velocity", "osc2.level", 22),
        pressure(24), R("poly_aftertouch", "osc3.level", 22),
        R("mod_wheel", "osc1.unison.detune", 40), R("mod_wheel", "filter1.cutoff", 10),
        R("macro1", "filter1.cutoff", 30), R("macro1", "filter1.resonance", 28),   # Bright
        R("macro2", "osc2.level", 65), R("macro2", "osc3.level", 55),       # Stack
        R("macro3", "osc1.unison.detune", 40), R("macro3", "hyperdim.detune", 30),    # Spread
        R("macro4", "delay.mix", 35), R("macro4", "hyperdim.mix", 30),      # Space
        R("macro5", "filter1.resonance", 40),                               # Edge
    ],
    ["Bright", "Stack", "Spread", "Space", "Edge", "", "", ""])

add("Bell FM Ripple Lead", "Lead", "fm,bell,envelope-modulation,velocity,digital",
    "A sine carrier phase-modulated from a second oscillator an octave and a fifth up: the index rides envelope 3 and the velocity, so soft notes are glassy and hard ones ring like struck metal",
    {
        "master.polyphony": 8,
        "osc1.on": 1, "osc1.wavetable": "Sine", "osc1.level": 70, "osc1.warp.type": "FM from Osc 2", "osc1.warp.amount": 14,
        "osc2.on": 1, "osc2.wavetable": "Sine", "osc2.level": 55, "osc2.transpose": 19, "osc2.destination": "None",
        "osc3.on": 1, "osc3.wavetable": "Bell Partials", "osc3.level": 22, "osc3.transpose": 12,
        "filter1.on": 1, "filter1.model": "Digital", "filter1.cutoff": 9000, "filter1.resonance": 10,
        "env1.attack": 2, "env1.decay": 1100, "env1.sustain": 55, "env1.release": 600,
        "env3.attack": 0, "env3.decay": 520, "env3.sustain": 12, "env3.release": 300,
        "lfo1.shape": "Sine", "lfo1.rate": 4.6, "lfo1.fade_in": 900,
        "delay.on": 1, "delay.mix": 22, "delay.sync": "Tempo", "delay.division": 6, "delay.feedback": 40, "delay.style": "Ping pong",
        "reverb.on": 1, "reverb.mix": 20, "reverb.decay": 2400, "reverb.type": "Hall",
        "chorus.on": 1, "chorus.mix": 14,
    },
    [
        R("env3", "osc1.warp.amount", 38),
        R("velocity", "osc1.warp.amount", 34), R("velocity", "osc3.level", 22),
        pressure(14), R("poly_aftertouch", "osc1.warp.amount", 42), R("poly_aftertouch", "osc3.level", 25),
        R("lfo1", "osc1.tune", 14, aux="mod_wheel"), R("lfo1", "osc3.tune", 14, aux="mod_wheel"), R("mod_wheel", "osc1.warp.amount", 16),
        R("macro1", "osc1.warp.amount", 45),                                # Index
        R("macro2", "env1.sustain", 45), R("macro2", "env1.release", 25),     # Ring
        R("macro3", "osc3.level", 40),                                      # Shimmer
        R("macro4", "delay.mix", 35), R("macro4", "reverb.mix", 35),        # Space
        R("macro5", "osc2.transpose", 14),                                  # Ratio
    ],
    ["Index", "Ring", "Shimmer", "Space", "Ratio", "", "", ""])
