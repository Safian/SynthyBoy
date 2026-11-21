# Batch 3, presets built on the Spectral wavetables that no earlier preset used. `add`, `R`, `pressure`, `WT`, `C` are provided by factory-presets-batch3.py.

add("Acoustic Bore Reed", "Lead", "lead,reed,clarinet,saxophone,wavetable,mono,legato,pressure,expressive",
    "A reed solo from a bore table: the frame axis crosses from odd harmonics only, a cylindrical bore as in a clarinet, to the full series, a conical bore as in a saxophone; the pad pressure and the wheel push the bore from the hollow to the full tone and open the filter, velocity opens it too; mono and legato with a glide, a late vibrato that the pressure brings, a touch of chorus, tempo echoes and a plate",
    {
        "master.polyphony": 1, "master.legato": 1, "master.portamento.time": 35,
        "osc1.on": 1, "osc1.wavetable": "Preset table", "osc1.frame": 0, "osc1.level": 66,
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 0, "filter1.cutoff": 4200, "filter1.resonance": 12, "filter1.keytrack": 55,
        "env1.attack": 25, "env1.decay": 300, "env1.sustain": 90, "env1.release": 180,
        "lfo1.shape": "Sine", "lfo1.rate": 5.0, "lfo1.fade_in": 600,
        "distortion.on": 1, "distortion.type": "Tube", "distortion.drive": 6, "distortion.mix": 0,
        "chorus.on": 1, "chorus.rate": 0.7, "chorus.depth": 30, "chorus.mix": 16,
        "delay.on": 1, "delay.sync": "Tempo", "delay.division": "1/4 note", "delay.feedback": 20, "delay.mix": 14,
        "reverb.on": 1, "reverb.type": "Plate", "reverb.decay": 1600, "reverb.mix": 20,
    },
    [
        R("poly_aftertouch", "osc1.frame", 60), pressure(32),                              # the pressure is the bore: clarinet to saxophone
        R("lfo1", "osc1.tune", 5, aux="poly_aftertouch"),                                  # a vibrato that the pressure brings
        R("mod_wheel", "osc1.frame", 45), R("mod_wheel", "filter1.cutoff", 20),
        R("velocity", "osc1.frame", 20), R("velocity", "filter1.cutoff", 16),
        R("macro1", "osc1.frame", 70),                                                     # Bore Morph
        R("macro2", "filter1.cutoff", -35),                                                # Dark
        R("macro3", "lfo1.rate", 40),                                                      # Vibrato Rate
        R("macro4", "distortion.mix", 40),                                                 # Grit
        R("macro5", "delay.mix", 30),                                                      # Echo
        R("macro6", "reverb.mix", 40), R("macro6", "reverb.decay", 20),                    # Room
    ],
    ["Bore Morph", "Dark", "Vibrato Rate", "Grit", "Echo", "Room", "", ""],
    content=C(wavetables=[WT(1, "Spectral/bore_odd_to_even")]))

add("Harmonic Wah Bass", "Bass", "bass,wah,talking,funk,spectral,wavetable,band-sweep,sub",
    "A talking funk bass: the table lifts one band of the harmonics by 24 dB and moves it up the series with the frame, so a slow triangle LFO on the frame is a wah that lives in the oscillator and the filter stays free; a sine sub keeps the bottom, a tube adds the throat; the wheel pushes the wah up, the first macro sets where it sits",
    {
        "master.polyphony": 4,
        "osc1.on": 1, "osc1.wavetable": "Preset table", "osc1.frame": 10, "osc1.level": 66,
        "sub.on": 1, "sub.waveform": "Sine", "sub.octave": -1, "sub.level": 45,
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 0, "filter1.cutoff": 2200, "filter1.resonance": 20, "filter1.drive": 4, "filter1.keytrack": 30,
        "env1.attack": 2, "env1.decay": 500, "env1.sustain": 85, "env1.release": 130,
        "lfo1.shape": "Triangle", "lfo1.sync": "Free (Hz)", "lfo1.rate": 2.2,
        "distortion.on": 1, "distortion.type": "Tube", "distortion.drive": 4.5, "distortion.mix": 45,
        "eq.on": 1, "eq.low.mode": "High pass", "eq.low.cutoff": 32,
        "utility.on": 1, "utility.mono_bass": 1, "utility.mono_freq": 150,
    },
    [
        R("lfo1", "osc1.frame", 40),                                                       # the wah
        R("velocity", "osc1.frame", 18), R("velocity", "filter1.cutoff", 16),
        R("mod_wheel", "osc1.frame", 35), R("mod_wheel", "distortion.drive", 20),
        pressure(30),
        R("macro1", "osc1.frame", 65),                                                     # Wah Position
        R("macro2", "lfo1.rate", 60),                                                      # Wah Rate
        R("macro3", "distortion.drive", 45),                                               # Tube Drive
        R("macro4", "sub.level", 40),                                                      # Sub
        R("macro5", "filter1.cutoff", -35),                                                # Dark
        R("macro6", "filter1.resonance", 40),                                              # Resonance
    ],
    ["Wah Position", "Wah Rate", "Tube Drive", "Sub", "Dark", "Resonance", "", ""],
    content=C(wavetables=[WT(1, "Spectral/harmonic_band_sweep")]))

add("Cathedral Bell Ring", "Keys", "keys,bell,cathedral,additive,bronze,wavetable,hall,velocity",
    "An additive cathedral bell: the table starts as the strike, with hum, prime, tierce and a cluster of nominals, and lets the high members fade along the frame axis down to the hum; the envelope puts the note on the strike and walks it to the hum, velocity moves the start so a hard key rings brighter; a long hall carries the ring",
    {
        "master.polyphony": 8,
        "osc1.on": 1, "osc1.wavetable": "Preset table", "osc1.frame": 62, "osc1.level": 64, "osc1.unison.voices": 2, "osc1.unison.detune": 4, "osc1.unison.spread": 65,
        "filter1.on": 1, "filter1.model": "Digital", "filter1.style": 0, "filter1.cutoff": 6200, "filter1.resonance": 10, "filter1.keytrack": 60,
        "env1.attack": 1, "env1.decay": 3600, "env1.sustain": 0, "env1.release": 1400,
        "env2.attack": 0, "env2.decay": 1800, "env2.sustain": 0,
        "reverb.on": 1, "reverb.type": "Hall", "reverb.decay": 3600, "reverb.mix": 32,
        "eq.on": 1, "eq.low.mode": "High pass", "eq.low.cutoff": 80,
    },
    [
        R("env2", "osc1.frame", -62),                                                      # the strike is frame 0, the hum is where it settles
        R("velocity", "osc1.frame", -25), R("velocity", "filter1.cutoff", 12),             # harder is brighter
        R("mod_wheel", "osc1.frame", -35), R("mod_wheel", "reverb.mix", 20),
        R("macro1", "osc1.frame", -45),                                                    # Bell Bright (0 = the hum)
        R("macro2", "env1.decay", 40),                                                     # Ring Length
        R("macro3", "filter1.cutoff", -45),                                                # Dampen
        R("macro4", "reverb.mix", 35), R("macro4", "reverb.decay", 20),                    # Hall
        R("macro5", "env2.decay", 40),                                                     # Strike Length
        R("macro6", "osc1.unison.detune", 55),                                             # Beating
    ],
    ["Bell Bright", "Ring Length", "Dampen", "Hall", "Strike Length", "Beating", "", ""],
    content=C(wavetables=[WT(1, "Spectral/additive_bell_strike")]))

add("Dispersion Phase Pad", "Pad", "pad,evolving,dispersion,phase,spectral,wavetable,chirp,swirl",
    "A pad whose spectrum never changes and whose sound does: the table keeps the same harmonic magnitudes and rotates their phases with the square of the harmonic number, so the wave sweeps from a saw through chirps to noise-like ripples without a filter; a slow LFO walks it, and a soft clip after the filter turns the changes of the phase into changes of the tone (the ear alone hardly hears a phase), three detuned voices and a sine an octave down, a chorus and a plate; kept an octave below the played key, where a chirp is slow enough to be heard",
    {
        "master.polyphony": 6, "master.transpose": -12,
        "osc1.on": 1, "osc1.wavetable": "Preset table", "osc1.frame": 8, "osc1.level": 62, "osc1.unison.voices": 3, "osc1.unison.detune": 10, "osc1.unison.spread": 75,
        "osc2.on": 1, "osc2.wavetable": "Sine", "osc2.level": 22, "osc2.transpose": -12,
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 0, "filter1.cutoff": 6000, "filter1.resonance": 16, "filter1.keytrack": 35,
        "env1.attack": 1400, "env1.decay": 1800, "env1.sustain": 90, "env1.release": 3000,
        "lfo1.shape": "Sine", "lfo1.rate": 0.12,
        "distortion.on": 1, "distortion.type": "Soft clip", "distortion.drive": 8, "distortion.mix": 40,
        "chorus.on": 1, "chorus.rate": 0.4, "chorus.depth": 45, "chorus.mix": 26,
        "reverb.on": 1, "reverb.type": "Plate", "reverb.decay": 3800, "reverb.mix": 30,
        "eq.on": 1, "eq.low.mode": "High pass", "eq.low.cutoff": 60,
    },
    [
        R("lfo1", "osc1.frame", 35),                                                       # the swirl
        pressure(36), R("poly_aftertouch", "osc1.frame", 14),
        R("velocity", "filter1.cutoff", 14),
        R("mod_wheel", "osc1.frame", 30), R("mod_wheel", "chorus.mix", 25),
        R("macro1", "osc1.frame", 70),                                                     # Phase Swirl
        R("macro2", "lfo1.rate", 50),                                                      # Swirl Speed
        R("macro3", "filter1.cutoff", -40),                                                # Dark
        R("macro4", "reverb.mix", 35), R("macro4", "reverb.decay", 20),                    # Space
        R("macro5", "distortion.drive", 50),                                               # Clip Drive
        R("macro6", "osc1.unison.detune", 60),                                             # Width
    ],
    ["Phase Swirl", "Swirl Speed", "Dark", "Space", "Clip Drive", "Width", "", ""],
    content=C(wavetables=[WT(1, "Spectral/dispersion_sweep")]))
