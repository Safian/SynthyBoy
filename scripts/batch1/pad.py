# Batch 1, pads and evolving textures. `add`, `R` and `pressure` are provided by factory-presets-batch1.py.

add("Stretched Glass Choir", "Pad", "pad,morph,harmonic-stretch,random-lfo,smear,glass",
    "Two layers whose partials are stretched by the spectral morph, drifting on random LFOs; a plate give it a glass choir body that breathes under the hand",
    {
        "master.polyphony": 8,
        "osc1.on": 1, "osc1.wavetable": "Sine to Saw", "osc1.frame": 40, "osc1.level": 55,
        "osc1.morph.type": "Harmonic Stretch", "osc1.morph.amount": 30,
        "osc2.on": 1, "osc2.wavetable": "Harmonic Series", "osc2.frame": 45, "osc2.level": 40, "osc2.transpose": 12, "osc2.unison.voices": 3, "osc2.unison.detune": 12, "osc2.unison.spread": 90,
        "filter1.on": 1, "filter1.model": "Digital", "filter1.style": 1, "filter1.cutoff": 2600, "filter1.resonance": 12, "filter1.keytrack": 40,
        "env1.attack": 1100, "env1.decay": 1500, "env1.sustain": 90, "env1.release": 2200,
        "rlfo1.style": "Perlin", "rlfo1.rate": 0.22, "rlfo2.style": "Perlin", "rlfo2.rate": 0.15, "rlfo3.style": "Sine interpolate", "rlfo3.rate": 0.3,
        "lfo1.shape": "Sine", "lfo1.rate": 0.19,
        "chorus.on": 1, "chorus.mix": 22, "chorus.rate": 0.3, "chorus.depth": 40,
        "reverb.on": 1, "reverb.type": "Plate", "reverb.mix": 28, "reverb.decay": 3200, "reverb.size": 70,
        "eq.on": 1, "eq.low.cutoff": 120, "eq.low.gain": -4, "eq.high.cutoff": 9000, "eq.high.gain": 2,
    },
    [
        R("rlfo1", "osc1.morph.amount", 40),                                # the one morph that moves (a morph under modulation is the dearest thing a voice can do: the other layer's is fixed)
        R("rlfo3", "osc2.frame", 16), R("rlfo2", "osc2.pan", 40),                # (the frame of an oscillator with a morph is left alone: any change of it rebuilds the morph cycle of every voice)
        R("lfo1", "filter1.cutoff", 8),
        R("velocity", "filter1.cutoff", 12),
        pressure(42), R("poly_aftertouch", "osc1.morph.amount", 30),
        R("mod_wheel", "osc2.level", 25), R("mod_wheel", "osc2.frame", 25),
        R("macro1", "filter1.cutoff", 45),                                  # Bright
        R("macro2", "osc1.morph.amount", 60),                                  # Stretch
        R("macro3", "osc2.unison.detune", 50), R("macro3", "chorus.mix", 30),         # Shimmer
        R("macro4", "reverb.mix", 35), R("macro4", "reverb.decay", 20),               # Space
        R("macro5", "env1.attack", 30), R("macro5", "env1.release", 25),              # Slow
    ],
    ["Bright", "Stretch", "Shimmer", "Space", "Slow", "", "", ""])

add("Mid Side Halo", "Pad", "pad,mid-side,splitter,compressor,chorus,wide",
    "A three-oscillator warm pad split into mid and side: the centre is compressed and EQ'd and stays dry and solid, the sides get chorus and a phaser, so the halo grows wide while the middle holds its place in mono",
    {
        "master.polyphony": 12,
        "osc1.on": 1, "osc1.wavetable": "Saw", "osc1.level": 45, "osc1.pan": -45, "osc1.unison.voices": 3, "osc1.unison.detune": 18, "osc1.unison.spread": 100,
        "osc2.on": 1, "osc2.wavetable": "Triangle", "osc2.level": 40, "osc2.pan": 40, "osc2.transpose": -12, "osc2.tune": 5,
        "osc3.on": 1, "osc3.wavetable": "Saw to Square", "osc3.frame": 50, "osc3.level": 32, "osc3.pan": 55, "osc3.transpose": 7, "osc3.tune": -6, "osc3.unison.voices": 3, "osc3.unison.detune": 14,
        "sub.on": 1, "sub.waveform": "Triangle", "sub.octave": -1, "sub.level": 30,
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 1, "filter1.cutoff": 1400, "filter1.resonance": 18, "filter1.keytrack": 40,
        "env1.attack": 650, "env1.decay": 1200, "env1.sustain": 88, "env1.release": 1500,
        "env2.attack": 900, "env2.decay": 1800, "env2.sustain": 45,
        "lfo1.shape": "Sine", "lfo1.rate": 0.23,
        "splitter_ms.on": 1,
        "compressor.on": 1, "compressor.rack": "Splitter M/S mid", "compressor.bands": "Single band", "compressor.mid.upper_threshold": -14, "compressor.mid.upper_ratio": 40, "compressor.attack": 40, "compressor.release": 60, "compressor.mid.gain": 3,
        "eq.on": 1, "eq.rack": "Splitter M/S mid", "eq.low.cutoff": 100, "eq.low.gain": -3, "eq.mid.cutoff": 2500, "eq.mid.gain": 2,
        "chorus.on": 1, "chorus.rack": "Splitter M/S side", "chorus.mix": 55, "chorus.rate": 0.25, "chorus.depth": 60,
        "phaser.on": 1, "phaser.rack": "Splitter M/S side", "phaser.mix": 35, "phaser.rate": 0.1, "phaser.depth": 26, "phaser.feedback": 40,
        "reverb.on": 1, "reverb.mix": 24, "reverb.decay": 3200, "reverb.type": "Hall",
        "utility.on": 1, "utility.mono_bass": 1, "utility.mono_freq": 150, "utility.width": 110,
    },
    [
        R("env2", "filter1.cutoff", 14),
        R("lfo1", "filter1.cutoff", 7), R("lfo1", "osc3.frame", 14),
        R("velocity", "filter1.cutoff", 14),
        pressure(40), R("poly_aftertouch", "osc3.level", 22),
        R("mod_wheel", "utility.width", 40), R("mod_wheel", "phaser.mix", 25),
        R("macro1", "filter1.cutoff", 45),                                  # Brightness
        R("macro2", "utility.width", 60), R("macro2", "chorus.mix", 40), R("macro2", "phaser.mix", 60),   # Halo
        R("macro3", "osc3.level", 40), R("macro3", "osc3.unison.detune", 30),   # Colour
        R("macro4", "reverb.mix", 35), R("macro4", "reverb.decay", 30),            # Space
        R("macro5", "sub.level", 40),                                           # Weight
    ],
    ["Brightness", "Halo", "Colour", "Space", "Weight", "", "", ""])

add("Parallel Dusk Pad", "Pad", "pad,parallel-filters,flanges,stereo-filters,evolving,phase-memory",
    "A slow pad whose two filters run in parallel on separate halves of the stereo field: an analog 24 dB low pass on the left, a Flanges comb on the right, an LFO opposing their cutoffs, so the image breathes",
    {
        "master.polyphony": 6,
        "osc1.on": 1, "osc1.wavetable": "Saw", "osc1.level": 50, "osc1.unison.voices": 2, "osc1.unison.detune": 16, "osc1.unison.spread": 60, "osc1.destination": "Filters 1+2",
        "osc2.on": 1, "osc2.wavetable": "Odd Harmonics", "osc2.level": 38, "osc2.transpose": 12, "osc2.destination": "Filters 1+2", "osc2.phase_memory": "Per Voice",
        "filter.routing": "Parallel",
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 1, "filter1.cutoff": 650, "filter1.resonance": 22, "filter1.pan": -80, "filter1.keytrack": 45,
        "filter2.on": 1, "filter2.model": "Flanges", "filter2.style": 7, "filter2.cutoff": 1800, "filter2.resonance": 35, "filter2.pan": 80, "filter2.keytrack": 45,
        "env1.attack": 900, "env1.decay": 1400, "env1.sustain": 85, "env1.release": 1800,
        "env4.attack": 2500, "env4.decay": 3000, "env4.sustain": 60, "env4.release": 3000,
        "lfo1.shape": "Triangle", "lfo1.rate": 0.17,
        "chorus.on": 1, "chorus.mix": 24, "chorus.rate": 0.2,
        "reverb.on": 1, "reverb.type": "Vintage", "reverb.mix": 30, "reverb.decay": 3600,
        "utility.on": 1, "utility.width": 140, "utility.hpf": 30,
    },
    [
        R("lfo1", "filter1.cutoff", 14), R("lfo1", "filter2.cutoff", -14),
        R("env4", "filter2.cutoff", 16),                                    # the second filter opens over the first seconds
        R("velocity", "filter1.cutoff", 12), R("velocity", "filter2.cutoff", 28), R("velocity", "osc2.level", 35),
        pressure(40), R("poly_aftertouch", "filter2.resonance", 22), R("poly_aftertouch", "filter2.cutoff", 25),
        R("mod_wheel", "filter2.cutoff", 25), R("mod_wheel", "filter1.cutoff", 25),
        R("macro1", "filter1.cutoff", 40),                                  # Left Open
        R("macro2", "filter2.cutoff", 35), R("macro2", "filter2.resonance", 25),   # Comb Colour
        R("macro3", "lfo1.rate", 40),                                       # Breath Rate
        R("macro4", "reverb.mix", 35), R("macro4", "chorus.mix", 30),       # Space
        R("macro5", "osc2.level", 40),                                      # Upper Layer
    ],
    ["Left Open", "Comb Colour", "Breath Rate", "Space", "Upper Layer", "", "", ""])

add("Brass Swell Ensemble", "Pad", "pad,brass,swell,three-oscillators,noise-breath,velocity",
    "A swelling brass-section pad: three detuned saws and a sub, a filter that opens with the note and with harder playing, breath noise on the front of the note and an EQ high pass that keeps the low mids clear",
    {
        "osc1.on": 1, "osc1.wavetable": "Saw", "osc1.level": 48, "osc1.unison.voices": 3, "osc1.unison.detune": 11, "osc1.unison.spread": 55,
        "osc2.on": 1, "osc2.wavetable": "Saw", "osc2.level": 42, "osc2.tune": 7, "osc2.transpose": 0, "osc2.unison.voices": 2, "osc2.unison.detune": 9, "osc2.pan": 20,
        "osc3.on": 1, "osc3.wavetable": "Saw", "osc3.level": 40, "osc3.tune": -8, "osc3.transpose": -12, "osc3.pan": -20,
        "sub.on": 1, "sub.waveform": "Triangle", "sub.octave": -1, "sub.level": 26,
        "noise.on": 1, "noise.level": 8, "noise.destination": "Filter 1",
        "master.polyphony": 8,
        "filter1.on": 1, "filter1.model": "Analog", "filter1.cutoff": 500, "filter1.resonance": 20, "filter1.keytrack": 55,
        "eq.on": 1, "eq.low.mode": "High pass", "eq.low.cutoff": 120, "eq.mid.cutoff": 600, "eq.mid.gain": -2,
        "env1.attack": 450, "env1.decay": 900, "env1.sustain": 90, "env1.release": 900,
        "env2.attack": 650, "env2.decay": 1100, "env2.sustain": 40, "env2.release": 800,
        "env3.attack": 0, "env3.decay": 400, "env3.sustain": 0,
        "chorus.on": 1, "chorus.mix": 28, "chorus.rate": 0.35,
        "reverb.on": 1, "reverb.type": "Hall", "reverb.mix": 16, "reverb.decay": 2000,
    },
    [
        R("env2", "filter1.cutoff", 42),                                    # the swell
        R("env3", "noise.level", 14),                                       # the breath at the front
        R("velocity", "filter1.cutoff", 24), R("velocity", "env2.attack", -20),
        pressure(45), R("poly_aftertouch", "osc1.unison.detune", 14),
        R("mod_wheel", "filter1.cutoff", 18), R("mod_wheel", "filter1.resonance", 15),
        R("macro1", "filter1.cutoff", 40),                                  # Bright
        R("macro2", "env2.attack", 35), R("macro2", "env1.attack", 25),     # Swell Time
        R("macro3", "noise.level", 25),                                     # Breath
        R("macro4", "reverb.mix", 38), R("macro4", "chorus.mix", 30),       # Hall
        R("macro5", "osc1.unison.detune", 40), R("macro5", "osc2.tune", 8), # Section
    ],
    ["Bright", "Swell Time", "Breath", "Hall", "Section", "", "", ""])
