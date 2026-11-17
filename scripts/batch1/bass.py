# Batch 1, basses. `add`, `R` and `pressure` are provided by factory-presets-batch1.py.

add("Subterranean Duo", "Bass", "sub,bass,french-lp,compressor,key-fold,eq",
    "A pure sine sub that goes round the filter, under a saw growl layer folded into two octaves, through the French LP model, an EQ high pass and a compressor",
    {
        "osc1.on": 1, "osc1.wavetable": "Saw", "osc1.level": 78, "osc1.unison.voices": 3, "osc1.unison.detune": 14, "osc1.unison.spread": 55,
        "master.polyphony": 6,
        "osc1.map.key_low": 36, "osc1.map.key_high": 59, "osc1.map.key_fold": 1,
        "sub.on": 1, "sub.waveform": "Sine", "sub.octave": 0, "sub.level": 70, "sub.destination": "Effects only",
        "filter1.on": 1, "filter1.model": "French LP", "filter1.cutoff": 480, "filter1.resonance": 38, "filter1.drive": 6, "filter1.keytrack": 35,
        "eq.on": 1, "eq.low.mode": "High pass", "eq.low.cutoff": 70, "eq.mid.cutoff": 320, "eq.mid.gain": -2,
        "env1.attack": 2, "env1.decay": 600, "env1.sustain": 100, "env1.release": 140,
        "env2.attack": 0, "env2.decay": 260, "env2.sustain": 0, "env2.release": 120,
        "compressor.on": 1, "compressor.bands": "Single band", "compressor.mid.upper_threshold": -26, "compressor.mid.upper_ratio": 55,
        "compressor.mix": 55, "compressor.attack": 35, "compressor.release": 55, "compressor.mid.gain": 4,
        "utility.on": 1, "utility.mono_bass": 1, "utility.mono_freq": 160, "utility.width": 120,
    },
    [
        R("env2", "filter1.cutoff", 24, label="snap"),         # the snap of every note
        R("velocity", "filter1.cutoff", 18),                   # touch: harder = brighter ...
        R("velocity", "filter1.drive", 30),                    # ... and angrier
        R("mod_wheel", "filter1.drive", 85), R("mod_wheel", "filter1.resonance", 30),
        R("mod_wheel", "filter1.cutoff", 12),
        pressure(18),
        R("macro1", "filter1.cutoff", 32),                     # Bite
        R("macro2", "filter1.drive", 60), R("macro2", "osc1.unison.detune", 25),   # Growl
        R("macro3", "@snap.amount", 40),                       # Punch: the depth of the filter snap
        R("macro4", "sub.level", 30),             # Sub Weight
        R("macro5", "compressor.mix", 45), R("macro5", "compressor.mid.gain", 12),   # Squeeze
    ],
    ["Bite", "Growl", "Punch", "Sub Weight", "Squeeze", "", "", ""])


add("Bend Formant Growl", "Bass", "growl,warp,bend,formant,parallel-filters,eq",
    "A saw bent by a free LFO that the wheel deepens, through an analog low pass and the Formant filter model side by side: a second LFO route sweeps the vowel so the growl talks; a diode edge and an EQ clean-up",
    {
        "osc1.on": 1, "osc1.wavetable": "Saw", "osc1.level": 72, "osc1.destination": "Filters 1+2",
        "master.polyphony": 6,
        "osc1.warp.type": "Bend", "osc1.warp.amount": 30,
        "sub.on": 1, "sub.waveform": "Sine", "sub.octave": 0, "sub.level": 55,
        "filter.routing": "Parallel",
        "filter1.on": 1, "filter1.model": "Analog", "filter1.cutoff": 900, "filter1.resonance": 35, "filter1.keytrack": 40,
        "filter2.on": 1, "filter2.model": "Formant", "filter2.cutoff": 1100, "filter2.resonance": 55, "filter2.keytrack": 30, "filter2.formant.x": 30, "filter2.formant.y": 45, "filter2.formant.resonance": 80,
        "lfo1.shape": "Triangle", "lfo1.rate": 3.2, "lfo2.shape": "Triangle", "lfo2.rate": 3.2,   # lfo 2 is lfo 1 again: the wheel route and the always-on route must not be the same pair (one route per source and destination)
        "env1.attack": 2, "env1.decay": 500, "env1.sustain": 100, "env1.release": 120,
        "env2.attack": 0, "env2.decay": 220, "env2.sustain": 0,
        "distortion.on": 1, "distortion.type": "Diode 1", "distortion.drive": 6, "distortion.mix": 70,
        "eq.on": 1, "eq.low.cutoff": 90, "eq.low.gain": 3, "eq.mid.cutoff": 420, "eq.mid.gain": -3, "eq.high.cutoff": 7000, "eq.high.gain": -2,
        "utility.on": 1, "utility.mono_bass": 1, "utility.mono_freq": 140,
    },
    [
        R("lfo1", "osc1.warp.amount", 24, label="g1"),                     # the growl always moves a little
        R("lfo2", "osc1.warp.amount", 46, aux="mod_wheel", label="g2"),     # the wheel opens it
        R("lfo1", "filter2.formant.x", 40, label="g3"), R("lfo1", "filter2.formant.y", -25),
        R("lfo1", "filter2.cutoff", 14),
        R("env2", "filter1.cutoff", 26),
        R("velocity", "filter2.formant.x", 14), R("velocity", "distortion.drive", 25),
        R("mod_wheel", "filter2.formant.resonance", 15), R("mod_wheel", "distortion.drive", 50),
        R("macro1", "filter2.formant.x", 55), R("macro1", "filter2.cutoff", 20),   # Mouth
        R("macro2", "@g1.amount", 75), R("macro2", "@g2.amount", 75), R("macro2", "@g3.amount", 60),   # Growl Depth
        R("macro3", "lfo1.rate", 45), R("macro3", "lfo2.rate", 45),                # Wobble Rate
        R("macro4", "distortion.drive", 55),                                # Dirt
        R("macro5", "sub.level", 30),                                       # Sub
    ],
    ["Mouth", "Growl Depth", "Wobble Rate", "Dirt", "Sub", "", "", ""])

add("Triple Fold Reese", "Bass", "reese,three-oscillators,wavefold,series-filters,add-bass,phaser",
    "Three oscillators: a detuned saw pair and a square an octave down, folded by a sine-fold distortion whose drive falls from the attack into the body; a 24 dB low pass into Add Bass, phased and mono below 120 Hz",
    {
        "osc1.on": 1, "osc1.wavetable": "Saw", "osc1.level": 50, "osc1.unison.voices": 3, "osc1.unison.detune": 22, "osc1.unison.spread": 70,
        "osc2.on": 1, "osc2.wavetable": "Saw", "osc2.level": 45, "osc2.tune": 9, "osc2.unison.voices": 2, "osc2.unison.detune": 14,
        "osc3.on": 1, "osc3.wavetable": "Saw to Square", "osc3.frame": 100, "osc3.transpose": -12, "osc3.level": 55,
        "filter.routing": "Series 1 to 2",
        "master.polyphony": 6,
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 1, "filter1.cutoff": 650, "filter1.resonance": 22, "filter1.keytrack": 30,
        "filter2.on": 1, "filter2.model": "Add Bass", "filter2.cutoff": 300, "filter2.resonance": 30,
        "lfo1.shape": "Sine", "lfo1.rate": 0.11,
        "rlfo1.style": "Perlin", "rlfo1.rate": 0.35,
        "env1.attack": 3, "env1.decay": 500, "env1.sustain": 100, "env1.release": 160,
        "env3.attack": 0, "env3.decay": 900, "env3.sustain": 0, "env3.release": 200,
        "phaser.on": 1, "phaser.mix": 30, "phaser.rate": 0.12, "phaser.depth": 20, "phaser.feedback": 40,
        "distortion.on": 1, "distortion.type": "Sine fold", "distortion.drive": 3, "distortion.mix": 55,
        "utility.on": 1, "utility.mono_bass": 1, "utility.mono_freq": 120, "utility.width": 130, "utility.hpf": 25,
    },
    [
        R("env3", "distortion.drive", 16),                                  # the snarl of the attack melts into the body (the fold of the effect follows the newest note)
        R("lfo1", "filter1.cutoff", 9),
        R("rlfo1", "osc2.tune", 4), R("rlfo1", "osc1.unison.detune", 10),   # the pair drifts and phases
        R("velocity", "distortion.drive", 8), R("velocity", "filter1.cutoff", 16),
        R("mod_wheel", "filter1.cutoff", 22), R("mod_wheel", "distortion.drive", 14),
        R("macro1", "distortion.drive", 30), R("macro1", "filter1.cutoff", 14), R("macro1", "distortion.mix", 25),   # Fold
        R("macro2", "osc1.unison.detune", 40), R("macro2", "osc2.tune", 6), # Phase Drift
        R("macro3", "filter1.cutoff", 30),                                  # Dark / Open
        R("macro4", "utility.width", 60),                                   # Width
        R("macro5", "distortion.drive", 50),                                # Drive
    ],
    ["Fold", "Phase Drift", "Open", "Width", "Drive", "", "", ""])

add("Pitch Drop 808", "Bass", "808,pitch-drop,envelope-modulation,click,eq,compressor",
    "A sine 808 whose pitch falls from above in a few milliseconds (envelope 3), a short pulse knock from envelope 4, tube drive so it speaks on small speakers, an EQ shelf and a compressor",
    {
        "master.polyphony": 1,
        "osc1.on": 1, "osc1.wavetable": "Sine", "osc1.level": 80,
        "osc2.on": 1, "osc2.wavetable": "Pulse 25%", "osc2.level": 0, "osc2.transpose": 12,
        "filter1.on": 1, "filter1.model": "Digital", "filter1.cutoff": 2400, "filter1.resonance": 10,
        "env1.attack": 0, "env1.decay": 1500, "env1.sustain": 0, "env1.release": 350, "env1.decay_curve": -4,
        "env3.attack": 0, "env3.decay": 110, "env3.sustain": 0, "env3.release": 60,
        "env4.attack": 0, "env4.decay": 45, "env4.sustain": 0, "env4.release": 30,
        "distortion.on": 1, "distortion.type": "Tube", "distortion.drive": 8, "distortion.mix": 55,
        "eq.on": 1, "eq.low.cutoff": 70, "eq.low.gain": 4, "eq.mid.cutoff": 350, "eq.mid.gain": -3,
        "compressor.on": 1, "compressor.bands": "Single band", "compressor.mid.upper_threshold": -22, "compressor.mid.upper_ratio": 45,
        "compressor.attack": 30, "compressor.release": 60, "compressor.mid.gain": 3,
    },
    [
        R("env3", "osc1.transpose", 10, label="drop"),                      # the pitch falls from ten semitones above
        R("env4", "osc2.level", 55),                                        # the knock
        R("velocity", "osc1.transpose", 8), R("velocity", "filter1.cutoff", 14),
        R("mod_wheel", "distortion.drive", 60),
        R("macro1", "@drop.amount", 35),                                   # Drop: how far the pitch falls
        R("macro2", "env1.decay", 30), R("macro2", "env1.release", 25),     # Tail
        R("macro3", "osc2.level", 35),                                      # Knock
        R("macro4", "distortion.drive", 60), R("macro4", "distortion.mix", 25),   # Grit
        R("macro5", "eq.low.gain", 60),                                    # Boom
    ],
    ["Drop", "Tail", "Knock", "Grit", "Boom", "", "", ""])
