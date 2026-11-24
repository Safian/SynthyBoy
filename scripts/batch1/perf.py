# Batch 1, two performance patches built around pad pressure and the macros. `add`, `R` and `pressure` are provided by factory-presets-batch1.py.

add("Bowed Pressure Strings", "Pad", "performance,pressure,strings,bow,vibrato,expressive",
    "Strings you play with the weight of your finger: harder pressure opens the filter, brings in the bow's scratch and a vibrato that was not there, and fades up a second, brighter layer; at rest it is a soft, slow-speaking ensemble",
    {
        "osc1.on": 1, "osc1.wavetable": "Saw", "osc1.level": 52, "osc1.unison.voices": 3, "osc1.unison.detune": 9, "osc1.unison.spread": 60,
        "osc2.on": 1, "osc2.wavetable": "Pulse 25%", "osc2.level": 0, "osc2.tune": 5, "osc2.unison.voices": 2, "osc2.unison.detune": 8,
        "noise.on": 1, "noise.level": 0, "noise.destination": "Filter 1",
        "filter1.on": 1, "filter1.model": "Analog", "filter1.cutoff": 500, "filter1.resonance": 24, "filter1.keytrack": 60,
        "env1.attack": 260, "env1.decay": 800, "env1.sustain": 100, "env1.release": 420,
        "lfo1.shape": "Sine", "lfo1.rate": 5.2, "lfo1.fade_in": 500, "lfo2.shape": "Sine", "lfo2.rate": 5.2, "lfo2.fade_in": 500,
        "chorus.on": 1, "chorus.mix": 18, "chorus.rate": 0.3,
        "reverb.on": 1, "reverb.type": "Hall", "reverb.mix": 18, "reverb.decay": 2200,
        "eq.on": 1, "eq.low.cutoff": 110, "eq.low.gain": -3, "eq.high.cutoff": 7500, "eq.high.gain": 2,
    },
    [
        R("poly_aftertouch", "filter1.cutoff", 46),                          # the pressure route of the shell's layer is this one
        R("poly_aftertouch", "noise.level", 14),                             # bow scratch
        R("poly_aftertouch", "osc2.level", 40),                              # the brighter layer
        R("lfo2", "osc1.tune", 14, aux="poly_aftertouch"), R("lfo2", "osc2.tune", 14, aux="poly_aftertouch"),   # vibrato grows with the pressure
        R("lfo1", "osc1.tune", 6), R("lfo1", "osc2.tune", 6),
        R("velocity", "filter1.cutoff", 18), R("velocity", "env1.attack", -22),
        R("mod_wheel", "filter1.cutoff", 20), R("mod_wheel", "filter1.resonance", 15),
        R("macro1", "filter1.cutoff", 35),                                   # Brightness
        R("macro2", "env1.attack", 35), R("macro2", "env1.release", 25),     # Bow Speed
        R("macro3", "noise.level", 26),                                      # Rosin
        R("macro4", "lfo1.rate", 40), R("macro4", "lfo2.rate", 40),               # Vibrato Rate
        R("macro5", "reverb.mix", 35), R("macro5", "chorus.mix", 30),        # Hall
    ],
    ["Brightness", "Bow Speed", "Rosin", "Vibrato Rate", "Hall", "", "", ""])

add("Wah Pressure Clav", "Keys", "performance,pressure,clav,wah,funk,band-pass,multi-svf",
    "A clav with a pedal under your fingertip: pressure on the pad sweeps a Multi SVF band pass wah and tightens the resonance, velocity sets how long the note rings, and an EQ high pass keeps the low end out of the way",
    {
        "master.polyphony": 8,
        "osc1.on": 1, "osc1.wavetable": "Pulse 25%", "osc1.level": 58, "osc1.unison.voices": 2, "osc1.unison.detune": 6,
        "osc2.on": 1, "osc2.wavetable": "Saw", "osc2.level": 34, "osc2.transpose": 12, "osc2.tune": 4,
        "filter1.on": 1, "filter1.model": "Multi SVF", "filter1.style": 0, "filter1.cutoff": 700, "filter1.resonance": 55, "filter1.keytrack": 35,
        "env1.attack": 0, "env1.decay": 520, "env1.sustain": 38, "env1.release": 140,
        "env2.attack": 0, "env2.decay": 180, "env2.sustain": 0,
        "phaser.on": 1, "phaser.mix": 22, "phaser.rate": 0.35, "phaser.depth": 20,
        "distortion.on": 1, "distortion.type": "Diode 1", "distortion.drive": 4, "distortion.mix": 45,
        "eq.on": 1, "eq.low.mode": "High pass", "eq.low.cutoff": 150, "eq.mid.cutoff": 1600, "eq.mid.gain": 2, "eq.high.cutoff": 8000, "eq.high.gain": 1.5,
    },
    [
        R("poly_aftertouch", "filter1.cutoff", 46, label="wah"),             # the wah pedal
        R("poly_aftertouch", "filter1.resonance", 18),
        R("env2", "filter1.cutoff", 14),                                     # the pick
        R("velocity", "env1.decay", 12), R("velocity", "filter1.cutoff", 18), R("velocity", "distortion.drive", 25),
        R("mod_wheel", "phaser.mix", 35),
        R("macro1", "@wah.amount", 30),                                      # Wah Range
        R("macro2", "filter1.resonance", 35),                                # Quack
        R("macro3", "env2.decay", 35), R("macro3", "osc1.level", 18),        # Pick
        R("macro4", "distortion.drive", 55), R("macro4", "distortion.mix", 30),   # Dirt
        R("macro5", "phaser.mix", 40), R("macro5", "phaser.depth", 30),      # Phase
    ],
    ["Wah Range", "Quack", "Pick", "Dirt", "Phase", "", "", ""])
