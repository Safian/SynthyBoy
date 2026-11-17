# Batch 2, the Multisample oscillator type (key-mapped instruments of the factory sample pack). `add`, `R`, `pressure`, `WT`, `SMP`, `IMP`, `INST`, `C` are provided by factory-presets-batch2.py.

add("Nylon Zone Pluck", "Pluck", "pluck,nylon,multisample,zones,voice-control,round-robin,guitar",
    "Plucked nylon strings in six key zones, one recording to a zone and every zone at its own pitch and brightness; the voice control gives each note of a run its own pan, tuning and cutoff in a repeating pattern of eight (a human hand never plays the same note twice), a short saw pluck sits under it and the wheel doubles the strings (two unison voices that drift apart)",
    {
        "master.polyphony": 10,
        "osc1.on": 1, "osc1.type": "Multisample", "osc1.level": 76, "osc1.multi.vel_track": 100, "osc1.unison.voices": 2, "osc1.unison.detune": 3, "osc1.unison.spread": 45,
        "osc2.on": 1, "osc2.wavetable": "Saw", "osc2.level": 16, "osc2.unison.voices": 2, "osc2.unison.detune": 8, "osc2.unison.spread": 40,
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 0, "filter1.cutoff": 2600, "filter1.resonance": 10, "filter1.keytrack": 60,
        "env1.attack": 0, "env1.decay": 900, "env1.sustain": 0, "env1.release": 380,
        "env2.attack": 0, "env2.decay": 200, "env2.sustain": 0,
        "delay.on": 1, "delay.sync": 2, "delay.division": 5, "delay.mix": 12, "delay.feedback": 30, "delay.style": "Ping pong",
        "reverb.on": 1, "reverb.type": "Plate", "reverb.mix": 18, "reverb.decay": 1900,
        "eq.on": 1, "eq.low.mode": "High pass", "eq.low.cutoff": 80,
        "voicectl.length": 8,
        "voicectl.s1.pan": -30, "voicectl.s2.pan": 25, "voicectl.s3.pan": -10, "voicectl.s4.pan": 40, "voicectl.s5.pan": -40, "voicectl.s6.pan": 10, "voicectl.s7.pan": -20, "voicectl.s8.pan": 30,
        "voicectl.s1.detune": 6, "voicectl.s2.detune": -5, "voicectl.s3.detune": 3, "voicectl.s4.detune": -8, "voicectl.s5.detune": 7, "voicectl.s6.detune": -3, "voicectl.s7.detune": 5, "voicectl.s8.detune": -6,
        "voicectl.s1.cutoff": 0, "voicectl.s2.cutoff": -12, "voicectl.s3.cutoff": 8, "voicectl.s4.cutoff": -20, "voicectl.s5.cutoff": 12, "voicectl.s6.cutoff": -6, "voicectl.s7.cutoff": 16, "voicectl.s8.cutoff": -14,
    },
    [
        R("env2", "filter1.cutoff", 14),                                                   # the pick
        R("velocity", "filter1.cutoff", 18), R("velocity", "osc2.level", 20),
        R("mod_wheel", "osc1.unison.detune", 40), R("mod_wheel", "delay.mix", 22),
        R("macro1", "filter1.cutoff", 55),                                                 # Bright
        R("macro2", "osc2.level", 55),                                                     # Saw Pluck
        R("macro3", "osc1.unison.detune", 60),                                             # Double
        R("macro4", "env1.decay", 40), R("macro4", "env1.release", 25),                    # Sustain
        R("macro5", "delay.mix", 38),                                                      # Echo
        R("macro6", "reverb.mix", 35), R("macro6", "reverb.decay", 25),                    # Room
    ],
    ["Bright", "Saw Pluck", "Double", "Sustain", "Echo", "Room", "", ""],
    content=C(instruments=[INST(1, "pluck_nylon")]))

add("Marimba Layer Keys", "Keys", "keys,marimba,mallet,multisample,velocity-layers,convolve,room",
    "A marimba in five key zones and two velocity layers: soft keys play the dark, round bars, hard keys the bright ones with the knock of the mallet in front, and the change between them is a real change of recording, not a filter; a small-room impulse response, a slow tremolo from the wheel",
    {
        "master.polyphony": 12,
        "osc1.on": 1, "osc1.type": "Multisample", "osc1.level": 78, "osc1.multi.vel_track": 60, "osc1.unison.voices": 2, "osc1.unison.detune": 4, "osc1.unison.spread": 30,
        "osc2.on": 1, "osc2.wavetable": "Sine", "osc2.level": 12, "osc2.transpose": 12,
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 0, "filter1.cutoff": 3500, "filter1.resonance": 8, "filter1.keytrack": 50,
        "env1.attack": 0, "env1.decay": 1400, "env1.sustain": 0, "env1.release": 300,
        "lfo1.shape": "Sine", "lfo1.rate": 5.5,
        "utility.on": 1, "utility.width": 120,
        "convolve.on": 1, "convolve.ir": "Preset IR", "convolve.mix": 26, "convolve.ir_gain": 0,
        "chorus.on": 1, "chorus.mix": 14, "chorus.rate": 0.3,
    },
    [
        R("velocity", "filter1.cutoff", 20),
        R("lfo1", "utility.pan", 10, aux="mod_wheel"),                                     # tremolo
        R("mod_wheel", "osc1.multi.timbre", 20), R("mod_wheel", "osc2.level", 25),
        R("macro1", "osc1.multi.timbre", 30),                                              # Zone Shift
        R("macro2", "convolve.mix", 45),                                                   # Room
        R("macro3", "osc2.level", 55),                                                     # Octave Sine
        R("macro4", "filter1.cutoff", -55),                                                # Mellow
        R("macro5", "env1.decay", 40), R("macro5", "env1.release", 30),                    # Ring
        R("macro6", "chorus.mix", 40), R("macro6", "chorus.depth", 25),                    # Chorus
    ],
    ["Zone Shift", "Room", "Octave Sine", "Mellow", "Ring", "Chorus", "", ""],
    content=C(instruments=[INST(1, "mallet_marimba")], impulses=[IMP("small_room")]))

add("Glass Zone Bells", "Keys", "keys,bells,glass,multisample,scale,hirajoshi,convolve,plate,conform",
    "Struck glass in four zones, locked to the Hirajoshi scale on D: whatever you play is moved to the nearest note of the scale, so a hand on the keys cannot hit a wrong one; the plate impulse response and a soft sub make a hall of it, the wheel shifts the zones",
    {
        "master.polyphony": 10, "master.scale.key": "D", "master.scale.type": "Hirajoshi", "master.scale.mode": "Conform",
        "osc1.on": 1, "osc1.type": "Multisample", "osc1.level": 72, "osc1.unison.voices": 2, "osc1.unison.detune": 6, "osc1.unison.spread": 60,
        "osc2.on": 1, "osc2.wavetable": "Bell Partials", "osc2.frame": 40, "osc2.level": 14, "osc2.transpose": 0,
        "sub.on": 1, "sub.waveform": "Sine", "sub.octave": -1, "sub.level": 14,
        "filter1.on": 1, "filter1.model": "Digital", "filter1.style": 0, "filter1.cutoff": 2800, "filter1.resonance": 8, "filter1.keytrack": 50,
        "env1.attack": 0, "env1.decay": 2600, "env1.sustain": 0, "env1.release": 800,
        "delay.on": 1, "delay.sync": 2, "delay.division": 5, "delay.mix": 14, "delay.feedback": 34, "delay.style": "Ping pong",
        "convolve.on": 1, "convolve.ir": "Preset IR", "convolve.mix": 36, "convolve.ir_gain": 0,
        "eq.on": 1, "eq.low.mode": "High pass", "eq.low.cutoff": 110,
    },
    [
        R("velocity", "filter1.cutoff", 20), R("velocity", "osc2.level", 14),
        R("mod_wheel", "osc1.multi.timbre", 25), R("mod_wheel", "convolve.mix", 25),
        R("macro1", "filter1.cutoff", 55),                                                 # Bright
        R("macro2", "convolve.mix", 45), R("macro2", "convolve.attack", 8),                # Plate
        R("macro3", "osc2.level", 55),                                                     # Bell Layer
        R("macro4", "delay.mix", 38),                                                      # Echo
        R("macro5", "env1.release", 40), R("macro5", "env1.decay", 25),                    # Ring
        R("macro6", "sub.level", 60),                                                      # Sub
    ],
    ["Bright", "Plate", "Bell Layer", "Echo", "Ring", "Sub", "", ""],
    content=C(instruments=[INST(1, "glass_bells")], impulses=[IMP("plate")]))
