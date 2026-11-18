# Batch 3, presets built on the Analog wavetables that no earlier preset used. `add`, `R`, `pressure`, `WT`, `C` are provided by factory-presets-batch3.py.

add("Analog Sweep Bass", "Bass", "bass,analog,filter-sweep,wavetable,mono-bass,sub",
    "A bass whose filter sweep is written into the table: the cutoff of a resonant four pole filter opens inside the cycle, a short envelope throws the frame up at the front of every note so the bark falls back into a round saw, the wheel keeps it open; a sine sub holds the floor and the bottom stays in the middle",
    {
        "master.polyphony": 4,
        "osc1.on": 1, "osc1.wavetable": "Preset table", "osc1.frame": 8, "osc1.level": 68, "osc1.unison.voices": 2, "osc1.unison.detune": 8, "osc1.unison.spread": 40,
        "sub.on": 1, "sub.waveform": "Sine", "sub.octave": -1, "sub.level": 38,
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 0, "filter1.cutoff": 5200, "filter1.resonance": 12, "filter1.drive": 3, "filter1.keytrack": 25,
        "env1.attack": 2, "env1.decay": 600, "env1.sustain": 85, "env1.release": 140,
        "env2.attack": 0, "env2.decay": 320, "env2.sustain": 0,
        "distortion.on": 1, "distortion.type": "Soft clip", "distortion.drive": 3.5, "distortion.mix": 40,
        "eq.on": 1, "eq.low.mode": "High pass", "eq.low.cutoff": 32,
        "utility.on": 1, "utility.mono_bass": 1, "utility.mono_freq": 140,
    },
    [
        R("env2", "osc1.frame", 45),                                                       # the bark at the front of the note
        R("velocity", "osc1.frame", 15), R("velocity", "filter1.cutoff", 16),
        R("mod_wheel", "osc1.frame", 35), R("mod_wheel", "distortion.drive", 25),
        pressure(30),
        R("macro1", "osc1.frame", 55),                                                     # Sweep
        R("macro2", "env2.decay", 45),                                                     # Bark Length
        R("macro3", "distortion.drive", 50),                                               # Drive
        R("macro4", "sub.level", 40),                                                      # Sub
        R("macro5", "filter1.cutoff", 50),                                                 # Cutoff
        R("macro6", "filter1.resonance", 40),                                              # Resonance
    ],
    ["Sweep", "Bark Length", "Drive", "Sub", "Cutoff", "Resonance", "", ""],
    content=C(wavetables=[WT(1, "Analog/saw_filter_sweep")]))

add("Trance Stack Lead", "Lead", "lead,analog,supersaw,trance,wavetable,unison,wide",
    "A supersaw frozen into one cycle: seven saws whose phases fan out along the frame axis, so the table itself is the width, three unison voices and a saw a fifth up make the anthem, a slow LFO breathes the spread and the wheel opens it, dotted echoes and a plate carry it",
    {
        "master.polyphony": 8,
        "osc1.on": 1, "osc1.wavetable": "Preset table", "osc1.frame": 30, "osc1.level": 64, "osc1.unison.voices": 3, "osc1.unison.detune": 12, "osc1.unison.spread": 75,
        "osc2.on": 1, "osc2.wavetable": "Saw", "osc2.level": 22, "osc2.transpose": 7, "osc2.tune": 4,
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 1, "filter1.cutoff": 5200, "filter1.resonance": 18, "filter1.keytrack": 60,
        "env1.attack": 6, "env1.decay": 500, "env1.sustain": 85, "env1.release": 280,
        "lfo1.shape": "Sine", "lfo1.rate": 0.4,
        "chorus.on": 1, "chorus.rate": 0.6, "chorus.depth": 45, "chorus.mix": 28,
        "delay.on": 1, "delay.sync": "Tempo dotted", "delay.division": "1/8 note", "delay.feedback": 35, "delay.mix": 18,
        "reverb.on": 1, "reverb.type": "Plate", "reverb.decay": 2400, "reverb.mix": 22,
    },
    [
        R("lfo1", "osc1.frame", 18),                                                       # the spread breathes
        R("velocity", "osc1.frame", 22), R("velocity", "filter1.cutoff", 15),
        R("mod_wheel", "osc1.frame", 30), R("mod_wheel", "filter1.cutoff", 20),
        pressure(34),
        R("macro1", "osc1.frame", 55),                                                     # Spread
        R("macro2", "filter1.cutoff", 45),                                                 # Cutoff
        R("macro3", "osc2.level", 50),                                                     # Fifth
        R("macro4", "reverb.mix", 35), R("macro4", "delay.mix", 25),                       # Space
        R("macro5", "osc1.unison.detune", 50),                                             # Detune
        R("macro6", "lfo1.rate", 50),                                                      # Breath Rate
    ],
    ["Spread", "Cutoff", "Fifth", "Space", "Detune", "Breath Rate", "", ""],
    content=C(wavetables=[WT(1, "Analog/saw_stack_spread")]))

add("Sync Sine Screamer", "Lead", "lead,analog,hard-sync,formant,mono,legato,scream",
    "A hard sync sine slave: the frame is the sync ratio, an envelope throws it up on every key and it falls to a singing note, so each note screams once and then sings; mono and legato with a glide, a saw an octave down for weight, a tube and dotted echoes",
    {
        "master.polyphony": 1, "master.legato": 1, "master.portamento.time": 45,
        "osc1.on": 1, "osc1.wavetable": "Preset table", "osc1.frame": 8, "osc1.level": 66,
        "osc2.on": 1, "osc2.wavetable": "Saw", "osc2.level": 26, "osc2.transpose": -12,
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 1, "filter1.cutoff": 4800, "filter1.resonance": 28, "filter1.drive": 4, "filter1.keytrack": 45,
        "env1.attack": 3, "env1.decay": 400, "env1.sustain": 90, "env1.release": 220,
        "env2.attack": 0, "env2.decay": 180, "env2.sustain": 0,
        "lfo1.shape": "Sine", "lfo1.rate": 5.3, "lfo1.fade_in": 500,
        "distortion.on": 1, "distortion.type": "Tube", "distortion.drive": 7, "distortion.mix": 60,
        "delay.on": 1, "delay.sync": "Tempo dotted", "delay.division": "1/8 note", "delay.feedback": 30, "delay.mix": 16,
    },
    [
        R("env2", "osc1.frame", 40),                                                       # the scream at the front of the note
        R("lfo1", "osc1.tune", 5), R("lfo1", "osc2.tune", 5),
        R("mod_wheel", "osc1.frame", 45), R("mod_wheel", "distortion.drive", 20),
        pressure(32), R("poly_aftertouch", "osc1.frame", 12),
        R("velocity", "osc1.frame", 15), R("velocity", "filter1.cutoff", 16),
        R("macro1", "osc1.frame", 55),                                                     # Sync
        R("macro2", "env2.decay", 50),                                                     # Scream Length
        R("macro3", "distortion.drive", 50),                                               # Tube Drive
        R("macro4", "osc2.level", 45),                                                     # Octave Down
        R("macro5", "filter1.cutoff", 45),                                                 # Cutoff
        R("macro6", "lfo1.rate", 40),                                                      # Vibrato Rate
    ],
    ["Sync", "Scream Length", "Tube Drive", "Octave Down", "Cutoff", "Vibrato Rate", "", ""],
    content=C(wavetables=[WT(1, "Analog/sync_sine_sweep")]))

add("Sync Saw Raw Bass", "Bass", "bass,analog,hard-sync,raw,mono,legato,sub",
    "A raw hard sync bass: a saw slave whose ratio is the frame, the envelope tears it open at the front of the key and it settles on a firm note, a sine sub underneath, a hard clip for the edge; legato with a short glide, the wheel and the first macro walk the ratio",
    {
        "master.polyphony": 4, "master.legato": 1, "master.portamento.time": 30,
        "osc1.on": 1, "osc1.wavetable": "Preset table", "osc1.frame": 4, "osc1.level": 64,
        "sub.on": 1, "sub.waveform": "Sine", "sub.octave": -1, "sub.level": 42,
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 0, "filter1.cutoff": 1800, "filter1.resonance": 24, "filter1.drive": 5, "filter1.keytrack": 30,
        "env1.attack": 2, "env1.decay": 500, "env1.sustain": 80, "env1.release": 120,
        "env2.attack": 0, "env2.decay": 240, "env2.sustain": 0,
        "distortion.on": 1, "distortion.type": "Hard clip", "distortion.drive": 4, "distortion.mix": 40,
        "eq.on": 1, "eq.low.mode": "High pass", "eq.low.cutoff": 32,
        "utility.on": 1, "utility.mono_bass": 1, "utility.mono_freq": 150,
    },
    [
        R("env2", "osc1.frame", 35), R("env2", "filter1.cutoff", 14),                      # the tear at the front of the note
        R("velocity", "osc1.frame", 15), R("velocity", "filter1.cutoff", 16),
        R("mod_wheel", "osc1.frame", 40), R("mod_wheel", "distortion.drive", 25),
        pressure(30),
        R("macro1", "osc1.frame", 55),                                                     # Sync Ratio
        R("macro2", "env2.decay", 45),                                                     # Tear Length
        R("macro3", "distortion.drive", 50),                                               # Clip Drive
        R("macro4", "sub.level", 40),                                                      # Sub
        R("macro5", "filter1.cutoff", 50),                                                 # Cutoff
        R("macro6", "filter1.resonance", 40),                                              # Resonance
    ],
    ["Sync Ratio", "Tear Length", "Clip Drive", "Sub", "Cutoff", "Resonance", "", ""],
    content=C(wavetables=[WT(1, "Analog/sync_saw_sweep")]))
