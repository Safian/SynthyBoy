# Batch 1, evolving textures and atmospheres. `add`, `R`, `pressure` and `limiter_last` are provided by factory-presets-batch1.py.

add("Shepard Ascent", "Pad", "texture,shepard,evolving,endless-rise,multi-svf,random-lfo",
    "A drone that seems to climb for ever: two layers an octave apart ride the Shepard Tone morph up an octave in turn under a slow saw LFO, the upper fading out as the lower arrives while a Multi SVF high pass creeps up and a Lorenz random LFO shifts the colour; press to speed the climb",
    {
        "master.polyphony": 4,
        "osc1.on": 1, "osc1.wavetable": "Saw", "osc1.level": 60,
        "osc1.morph.type": "Shepard Tone", "osc1.morph.amount": 0,
        "osc2.on": 1, "osc2.wavetable": "Saw", "osc2.level": 0, "osc2.transpose": -12, "osc2.morph.type": "Shepard Tone", "osc2.morph.amount": 0,
        "filter.routing": "Series 1 to 2",
        "filter1.on": 1, "filter1.model": "Analog", "filter1.style": 1, "filter1.cutoff": 2600, "filter1.resonance": 30,
        "filter2.on": 1, "filter2.model": "Multi SVF", "filter2.style": 5, "filter2.cutoff": 500, "filter2.resonance": 15,
        "env1.attack": 3200, "env1.decay": 1500, "env1.sustain": 100, "env1.release": 3600,
        "lfo1.shape": "Saw Up", "lfo1.rate": 0.085, "lfo1.sync_type": "Retrigger",
        "lfo2.shape": "Triangle", "lfo2.rate": 0.04,
        "rlfo1.style": "Lorenz attractor", "rlfo1.rate": 0.12,
        "chorus.on": 1, "chorus.mix": 24, "chorus.rate": 0.15,
        "delay.on": 1, "delay.sync": "Tempo dotted", "delay.division": 5, "delay.mix": 20, "delay.feedback": 48, "delay.style": "Ping pong",
        "reverb.on": 1, "reverb.type": "Basin", "reverb.mix": 32, "reverb.decay": 4800, "reverb.size": 80,
    },
    [
        R("lfo1", "osc1.morph.amount", 100), R("lfo1", "osc2.morph.amount", 100),       # both layers climb an octave per cycle ...
        R("lfo1", "osc1.level", -77), R("lfo1", "osc2.level", 77),                       # ... the upper one fades out as the lower one arrives where it began: no seam
        R("lfo2", "filter2.cutoff", 14),
        R("rlfo1", "filter1.cutoff", 14),
        R("velocity", "filter1.cutoff", 10),
        pressure(35), R("poly_aftertouch", "lfo1.rate", 40),                 # pressure speeds the climb
        R("mod_wheel", "filter2.cutoff", 22),
        R("macro1", "filter1.cutoff", 40),                                   # Bright
        R("macro2", "lfo1.rate", 55),                                        # Climb Speed
        R("macro3", "filter2.cutoff", 30), R("macro3", "filter2.resonance", 30),   # Thin
        R("macro4", "reverb.mix", 30), R("macro4", "delay.mix", 30),         # Space
        R("macro5", "osc2.tune", 14), R("macro5", "rlfo1.rate", 55),             # Drift
    ],
    ["Bright", "Climb Speed", "Thin", "Space", "Drift", "", "", ""])

add("Lorenz Garden", "Pad", "texture,generative,random-lfo,lorenz,rossler,bell,sparkle",
    "A slow bed of bell partials and harmonic series where three random LFOs (Lorenz, Rossler, Perlin) wander over the wavetable frame, the pan and the filter, so no two moments sound the same; only chorus and delay, nothing that depends on the reverb",
    {
        "master.polyphony": 6,
        "osc1.on": 1, "osc1.wavetable": "Bell Partials", "osc1.level": 34, "osc1.unison.voices": 3, "osc1.unison.detune": 8, "osc1.unison.spread": 100, "osc1.transpose": 12,
        "osc2.on": 1, "osc2.wavetable": "Harmonic Series", "osc2.frame": 30, "osc2.level": 40, "osc2.unison.voices": 2, "osc2.unison.detune": 7,
        "osc3.on": 1, "osc3.wavetable": "Formant Sweep", "osc3.frame": 20, "osc3.level": 22, "osc3.transpose": -12,
        "sub.on": 1, "sub.waveform": "Sine", "sub.octave": -1, "sub.level": 24,
        "filter.routing": "Series 1 to 2",
        "filter1.on": 1, "filter1.model": "Smear", "filter1.style": 1, "filter1.cutoff": 3200, "filter1.resonance": 20,
        "filter2.on": 1, "filter2.model": "Analog", "filter2.style": 1, "filter2.cutoff": 3800, "filter2.resonance": 12,
        "env1.attack": 2600, "env1.decay": 1500, "env1.sustain": 100, "env1.release": 3200,
        "rlfo1.style": "Lorenz attractor", "rlfo1.rate": 0.18, "rlfo2.style": "Rossler attractor", "rlfo2.rate": 0.13, "rlfo3.style": "Perlin", "rlfo3.rate": 0.31, "rlfo4.style": "Sine interpolate", "rlfo4.rate": 0.09,
        "chorus.on": 1, "chorus.mix": 30, "chorus.rate": 0.17, "chorus.depth": 60,
        "delay.on": 1, "delay.sync": "Tempo dotted", "delay.division": 5, "delay.mix": 26, "delay.feedback": 52, "delay.style": "Ping pong",
        "reverb.on": 1, "reverb.type": "Plate", "reverb.mix": 22, "reverb.decay": 3600,
        "eq.on": 1, "eq.low.cutoff": 140, "eq.low.gain": -5, "eq.high.cutoff": 9000, "eq.high.gain": -2,
    },
    [
        R("rlfo1", "osc1.frame", 45), R("rlfo1", "osc1.pan", 50),
        R("rlfo2", "osc2.frame", 55), R("rlfo2", "osc3.frame", 40), R("rlfo2", "filter1.cutoff", 16),
        R("rlfo3", "osc2.pan", 60), R("rlfo3", "filter2.cutoff", 14),
        R("rlfo4", "osc3.level", 28),
        R("velocity", "filter1.cutoff", 10), R("velocity", "osc1.level", 35), R("velocity", "filter2.cutoff", 22),
        pressure(40), R("poly_aftertouch", "osc1.level", 30),
        R("mod_wheel", "osc1.level", 25), R("mod_wheel", "filter2.cutoff", 18),
        R("macro1", "filter1.cutoff", 40), R("macro1", "filter2.cutoff", 30),   # Bright
        R("macro2", "rlfo1.rate", 50), R("macro2", "rlfo2.rate", 50),           # Wander Speed
        R("macro3", "osc1.level", 40), R("macro3", "osc1.transpose", 12),       # Sparkle
        R("macro4", "delay.mix", 35), R("macro4", "chorus.mix", 30),            # Echo
        R("macro5", "sub.level", 40),                                           # Depth
    ],
    ["Bright", "Wander Speed", "Sparkle", "Echo", "Depth", "", "", ""])

add("Breath of Machines", "FX", "atmosphere,noise,formant,comb,sample-hold,breathing,industrial",
    "Noise through a formant filter that a Lorenz LFO sweeps, then a keytracked comb that gives the hum a pitch, and a sample-and-hold stepping the colour: a machine room that breathes slowly in and out",
    {
        "osc1.on": 0,
        "noise.on": 1, "noise.level": 60, "noise.destination": "Filter 1", "noise.stereo": 60,
        "osc2.on": 1, "osc2.wavetable": "Saw", "osc2.level": 12, "osc2.transpose": -12,
        "filter.routing": "Series 1 to 2",
        "filter1.on": 1, "filter1.model": "Formant", "filter1.cutoff": 1500, "filter1.resonance": 55, "filter1.formant.x": 40, "filter1.formant.y": 55, "filter1.formant.resonance": 85,
        "filter2.on": 1, "filter2.model": "Comb", "filter2.cutoff": 130.8, "filter2.resonance": 60, "filter2.keytrack": 100,
        "env1.attack": 2400, "env1.decay": 2000, "env1.sustain": 100, "env1.release": 3200,
        "lfo1.shape": "Sine", "lfo1.rate": 0.2,
        "rlfo1.style": "Lorenz attractor", "rlfo1.rate": 0.23, "rlfo2.style": "Sample and hold", "rlfo2.rate": 0.9,
        "delay.on": 1, "delay.sync": "Tempo", "delay.division": 6, "delay.mix": 16, "delay.feedback": 40,
        "reverb.on": 1, "reverb.type": "Nitrous", "reverb.mix": 26, "reverb.decay": 4200,
        "eq.on": 1, "eq.low.cutoff": 90, "eq.low.gain": -6, "eq.mid.cutoff": 1800, "eq.mid.gain": 2, "eq.high.cutoff": 7000, "eq.high.gain": -3,
    },
    [
        R("lfo1", "noise.level", 24), R("lfo1", "filter1.cutoff", 12),       # the breath
        R("rlfo1", "filter1.formant.x", 55), R("rlfo1", "filter1.formant.y", 40),
        R("rlfo2", "filter2.cutoff", 8),
        R("velocity", "filter1.cutoff", 12), R("velocity", "filter1.formant.x", 28), R("velocity", "filter2.cutoff", 14), R("velocity", "osc2.level", 40), R("velocity", "filter1.formant.y", 30),
        pressure(35), R("poly_aftertouch", "filter2.resonance", 28),
        R("mod_wheel", "noise.level", 22),
        R("macro1", "filter1.formant.x", 50),                                # Vowel
        R("macro2", "lfo1.rate", 50),                                        # Breath Rate
        R("macro3", "filter2.resonance", 35), R("macro3", "osc2.level", 40), # Hum
        R("macro4", "reverb.mix", 35), R("macro4", "delay.mix", 30),         # Room
        R("macro5", "rlfo2.rate", 60),                                       # Stutter
    ],
    ["Vowel", "Breath Rate", "Hum", "Room", "Stutter", "", "", ""])
