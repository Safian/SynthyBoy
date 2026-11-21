# Batch 3, presets built on the Metallic wavetables (no earlier preset used any of them). `add`, `R`, `pressure`, `WT`, `C` are provided by factory-presets-batch3.py.

add("Shimmer Bell Walk", "Keys", "keys,bell,metallic,shimmer,chime,wavetable,velocity,plate",
    "A church bell family in one table: the prime of twelve partials walks from the second to the twelfth harmonic along the frame axis, so a soft key is a dull clang and a hard key a glittering shimmer; the strike lands high and an envelope lets it fall back to the hum, a slow chorus and a long plate carry the ring",
    {
        "master.polyphony": 10,
        "osc1.on": 1, "osc1.wavetable": "Preset table", "osc1.frame": 12, "osc1.level": 62, "osc1.unison.voices": 2, "osc1.unison.detune": 5, "osc1.unison.spread": 60,
        "filter1.on": 1, "filter1.model": "Digital", "filter1.style": 0, "filter1.cutoff": 5500, "filter1.resonance": 12, "filter1.keytrack": 60,
        "env1.attack": 1, "env1.decay": 2600, "env1.sustain": 0, "env1.release": 900,
        "env2.attack": 0, "env2.decay": 700, "env2.sustain": 0,
        "chorus.on": 1, "chorus.rate": 0.5, "chorus.depth": 50, "chorus.mix": 24,
        "reverb.on": 1, "reverb.type": "Plate", "reverb.decay": 2800, "reverb.mix": 30,
        "eq.on": 1, "eq.low.mode": "High pass", "eq.low.cutoff": 90,
    },
    [
        R("env2", "osc1.frame", 40),                                                       # the strike lands high and falls back to the hum
        R("velocity", "osc1.frame", 32), R("velocity", "filter1.cutoff", 14),              # harder is brighter
        R("mod_wheel", "osc1.frame", 30), R("mod_wheel", "chorus.mix", 25),
        R("macro1", "osc1.frame", 60),                                                     # Harmonic Walk
        R("macro2", "env1.decay", 40),                                                     # Ring Length
        R("macro3", "filter1.cutoff", -45),                                                # Dampen
        R("macro4", "reverb.mix", 35), R("macro4", "reverb.decay", 20),                    # Space
        R("macro5", "env2.decay", 40),                                                     # Strike Length
        R("macro6", "osc1.unison.detune", 55),                                             # Shimmer
    ],
    ["Harmonic Walk", "Ring Length", "Dampen", "Space", "Strike Length", "Shimmer", "", ""],
    content=C(wavetables=[WT(1, "Metallic/bell_pitch_walk")]))

add("Free Bar Vibraphone", "Keys", "keys,vibraphone,mallet,metallic,bars,tremolo,wavetable",
    "Bars of a vibraphone from the modes of a free bar (the ratios 1, 2.76, 5.40, 8.93 ...): the frame is the hardness of the mallet and velocity moves it, an envelope gives the brief bright ping of the strike, the long ring is a sine sub and a vintage room; the wheel switches the motor, the tremolo of the resonators, on",
    {
        "master.polyphony": 10,
        "osc1.on": 1, "osc1.wavetable": "Preset table", "osc1.frame": 8, "osc1.level": 66,
        "sub.on": 1, "sub.waveform": "Sine", "sub.octave": -1, "sub.level": 14,
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 0, "filter1.cutoff": 3800, "filter1.resonance": 8, "filter1.keytrack": 50,
        "env1.attack": 1, "env1.decay": 3200, "env1.sustain": 0, "env1.release": 600,
        "env2.attack": 0, "env2.decay": 420, "env2.sustain": 0,
        "lfo1.shape": "Sine", "lfo1.rate": 5.0,
        "chorus.on": 1, "chorus.rate": 0.4, "chorus.depth": 30, "chorus.mix": 14,
        "reverb.on": 1, "reverb.type": "Vintage", "reverb.decay": 1500, "reverb.mix": 20,
        "eq.on": 1, "eq.low.mode": "High pass", "eq.low.cutoff": 60,
    },
    [
        R("env2", "osc1.frame", 25),                                                       # the ping of the strike
        R("lfo1", "osc1.level", 4), R("lfo1", "osc1.level", 18, aux="mod_wheel"),          # the motor: the wheel turns the tremolo up
        R("velocity", "osc1.frame", 35), R("velocity", "filter1.cutoff", 14),
        R("mod_wheel", "chorus.mix", 20),
        R("macro1", "osc1.frame", 55),                                                     # Mallet Hard
        R("macro2", "lfo1.rate", 50),                                                      # Motor Speed
        R("macro3", "sub.level", 35),                                                      # Resonator
        R("macro4", "reverb.mix", 30), R("macro4", "reverb.decay", 20),                    # Space
        R("macro5", "env1.decay", 40),                                                     # Ring Length
        R("macro6", "filter1.cutoff", -40),                                                # Dampen
    ],
    ["Mallet Hard", "Motor Speed", "Resonator", "Space", "Ring Length", "Dampen", "", ""],
    content=C(wavetables=[WT(1, "Metallic/free_bar_modes")]))

add("Gong Bloom Pad", "Pad", "pad,gong,bloom,swell,metallic,evolving,basin",
    "The bloom of a struck gong as a pad: the table holds the thirteen modes of a gong and lets the upper ones swell from 45 percent to full weight along the frame axis, the slow amp envelope walks that axis, so every chord opens from a dark hum into a bright bronze; a sine an octave down is the floor, a slow phaser and a basin reverb are the room",
    {
        "master.polyphony": 6,
        "osc1.on": 1, "osc1.wavetable": "Preset table", "osc1.frame": 0, "osc1.level": 60, "osc1.unison.voices": 2, "osc1.unison.detune": 6, "osc1.unison.spread": 70,
        "osc2.on": 1, "osc2.wavetable": "Sine", "osc2.level": 22, "osc2.transpose": -12,
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 0, "filter1.cutoff": 3200, "filter1.resonance": 14, "filter1.keytrack": 30,
        "env1.attack": 1800, "env1.decay": 2000, "env1.sustain": 90, "env1.release": 3500,
        "phaser.on": 1, "phaser.rate": 0.15, "phaser.depth": 20, "phaser.mix": 22,
        "reverb.on": 1, "reverb.type": "Basin", "reverb.decay": 4500, "reverb.size": 80, "reverb.mix": 34,
        "eq.on": 1, "eq.low.mode": "High pass", "eq.low.cutoff": 60,
    },
    [
        R("env1", "osc1.frame", 50),                                                       # the bloom follows the swell
        pressure(36), R("poly_aftertouch", "osc1.frame", 14),
        R("velocity", "filter1.cutoff", 14),
        R("mod_wheel", "osc1.frame", 30), R("mod_wheel", "phaser.mix", 25),
        R("macro1", "osc1.frame", 60),                                                     # Bloom
        R("macro2", "env1.attack", 50),                                                    # Swell Time
        R("macro3", "osc2.level", 40),                                                     # Drone
        R("macro4", "reverb.mix", 30), R("macro4", "reverb.decay", 20),                    # Space
        R("macro5", "filter1.cutoff", -40),                                                # Dark
        R("macro6", "osc1.unison.detune", 60),                                             # Shimmer
    ],
    ["Bloom", "Swell Time", "Drone", "Space", "Dark", "Shimmer", "", ""],
    content=C(wavetables=[WT(1, "Metallic/gong_modes_swell")]))

add("Cymbal Cloud Riser", "FX", "fx,riser,cymbal,metallic,cloud,sweep,flanger",
    "A riser of thirty-six wobbling metal partials: the frame axis moves each partial a little over the scan, so one note swells from a dull hum into a blinding shimmering cloud over four seconds and is cut at the top; three detuned voices, the filter opens with the swell, a flanger jets through it and dotted echoes with a plate wash the end",
    {
        "master.polyphony": 2,
        "osc1.on": 1, "osc1.wavetable": "Preset table", "osc1.frame": 0, "osc1.level": 64, "osc1.unison.voices": 3, "osc1.unison.detune": 18, "osc1.unison.spread": 80,
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 1, "filter1.cutoff": 1200, "filter1.resonance": 24, "filter1.keytrack": 0,
        "env1.attack": 4200, "env1.decay": 700, "env1.sustain": 0, "env1.release": 1200,
        "env2.attack": 4200, "env2.decay": 1500, "env2.sustain": 0, "env2.release": 800,
        "flanger.on": 1, "flanger.rate": 0.2, "flanger.depth": 60, "flanger.mix": 30,
        "delay.on": 1, "delay.sync": "Tempo dotted", "delay.division": "1/8 note", "delay.feedback": 45, "delay.mix": 22,
        "reverb.on": 1, "reverb.type": "Plate", "reverb.decay": 3500, "reverb.mix": 32,
        "utility.on": 1, "utility.level": 6.0,
    },
    [
        R("env1", "osc1.frame", 70),                                                       # the cloud opens with the swell
        R("env2", "filter1.cutoff", 40),
        R("mod_wheel", "osc1.frame", 30), R("mod_wheel", "filter1.cutoff", 25),
        R("velocity", "filter1.cutoff", 10),
        R("macro1", "osc1.frame", 80),                                                     # Cloud Motion
        R("macro2", "env1.attack", 50), R("macro2", "env2.attack", 50),                    # Riser Length
        R("macro3", "flanger.mix", 40),                                                    # Flange Jet
        R("macro4", "delay.mix", 30), R("macro4", "reverb.mix", 25),                       # Echo Wash
        R("macro5", "filter1.cutoff", 45),                                                 # Bright
        R("macro6", "osc1.unison.detune", 55),                                             # Width
    ],
    ["Cloud Motion", "Riser Length", "Flange Jet", "Echo Wash", "Bright", "Width", "", ""],
    content=C(wavetables=[WT(1, "Metallic/cymbal_cloud")]))
