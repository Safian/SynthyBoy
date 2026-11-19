# Batch 1, effects and one-shots. `add`, `R`, `pressure` and `limiter_last` are provided by factory-presets-batch1.py.

add("Bode Barber Riser", "FX", "riser,bode,frequency-shifter,noise,build,sweep,multi-svf",
    "A five second riser: a saw that climbs two octaves, noise through a rising Multi SVF high pass, and the Bode frequency shifter fed back on itself so the whole thing seems to rise faster than it does; it holds at the top until the key is released",
    {
        "master.polyphony": 4,
        "osc1.on": 1, "osc1.wavetable": "Saw", "osc1.level": 46, "osc1.unison.voices": 2, "osc1.unison.detune": 14, "osc1.unison.spread": 80,
        "osc2.on": 1, "osc2.wavetable": "Sine to Saw", "osc2.frame": 70, "osc2.level": 30, "osc2.transpose": 12,
        "noise.on": 1, "noise.level": 28, "noise.destination": "Filter 1",
        "filter1.on": 1, "filter1.model": "Multi SVF", "filter1.style": 5, "filter1.cutoff": 600, "filter1.resonance": 25,
        "env1.attack": 4200, "env1.decay": 1000, "env1.sustain": 100, "env1.release": 450,
        "env3.attack": 4800, "env3.decay": 1000, "env3.sustain": 100, "env3.release": 600, "env3.attack_curve": 3,
        "bode.on": 1, "bode.shift": 25, "bode.range": 700, "bode.dir": 100, "bode.feed": 42, "bode.blur": 25, "bode.mix": 55,
        "flanger.on": 1, "flanger.mix": 30, "flanger.rate": 0.25, "flanger.feedback": 45,
        "reverb.on": 1, "reverb.type": "Nitrous", "reverb.mix": 26, "reverb.decay": 3800,
    },
    [
        R("env3", "osc1.transpose", 19, label="rise"), R("env3", "osc2.transpose", 19),        # two octaves up over the attack
        R("env3", "filter1.cutoff", 34),
        R("env3", "bode.shift", 52, label="shift"),
        R("env3", "noise.level", 20),
        R("velocity", "filter1.cutoff", 14),
        R("poly_aftertouch", "bode.feed", 30), R("poly_aftertouch", "noise.level", 22),     # pressure: the shifter feeds back harder and the hiss comes up (no pressure route to the cutoff: it is a high pass here)
        R("mod_wheel", "bode.mix", 30), R("mod_wheel", "flanger.mix", 25),
        R("macro1", "@rise.amount", 25),                                      # Height
        R("macro2", "bode.shift", 45), R("macro2", "bode.mix", 30), R("macro2", "bode.feed", 30),       # Spin
        R("macro3", "noise.level", 40),                                       # Noise
        R("macro4", "reverb.mix", 35), R("macro4", "flanger.mix", 25),        # Space
        R("macro5", "env1.attack", 20), R("macro5", "env3.attack", 20),       # Length
    ],
    ["Height", "Spin", "Noise", "Space", "Length", "", "", ""])

add("Detonation Tail", "FX", "impact,hit,one-shot,splitter,limiter,cinematic",
    "A cinematic impact: a noise burst, a sine boom whose pitch collapses, and a split at 300 Hz so the low end is saturated while the highs go into a delay and a big reverb; a limiter closes the chain",
    {
        "master.polyphony": 4, "master.transpose": -24,
        "osc1.on": 1, "osc1.wavetable": "Sine", "osc1.level": 78,
        "osc2.on": 1, "osc2.wavetable": "Saw", "osc2.level": 22, "osc2.transpose": -12,
        "noise.on": 1, "noise.level": 70, "noise.destination": "Filter 1",
        "filter1.on": 1, "filter1.model": "Digital", "filter1.cutoff": 6000, "filter1.resonance": 14,
        "env1.attack": 0, "env1.decay": 2600, "env1.sustain": 0, "env1.release": 700, "env1.decay_curve": -4,
        "env2.attack": 0, "env2.decay": 420, "env2.sustain": 0,
        "env3.attack": 0, "env3.decay": 170, "env3.sustain": 0,
        "env4.attack": 0, "env4.decay": 360, "env4.sustain": 0,
        "splitter_lh.on": 1, "splitter_lh.freq": 300,
        "distortion.on": 1, "distortion.rack": "Splitter L/H low", "distortion.type": "Tube", "distortion.drive": 12, "distortion.mix": 70,
        "delay.on": 1, "delay.rack": "Splitter L/H high", "delay.sync": "Tempo dotted", "delay.division": 5, "delay.mix": 28, "delay.feedback": 46, "delay.style": "Ping pong",
        "reverb.on": 1, "reverb.rack": "Splitter L/H high", "reverb.type": "Hall", "reverb.mix": 38, "reverb.decay": 6000, "reverb.size": 90,
        "eq.on": 1, "eq.low.cutoff": 60, "eq.low.gain": 4, "eq.mid.cutoff": 400, "eq.mid.gain": -3,
        **limiter_last(-8, 100),
    },
    [
        R("env3", "osc1.transpose", 22, label="fall"),                        # the boom falls from two octaves above
        R("env2", "filter1.cutoff", 30),
        R("env4", "noise.level", 28),
        R("velocity", "filter1.cutoff", 14), R("velocity", "noise.level", 18),
        R("mod_wheel", "reverb.mix", 30),
        R("macro1", "noise.level", 30), R("macro1", "env4.decay", 20),        # Impact
        R("macro2", "osc1.level", 22), R("macro2", "@fall.amount", 20),       # Boom
        R("macro3", "env1.decay", 30), R("macro3", "reverb.decay", 25),       # Tail
        R("macro4", "distortion.drive", 55),                                  # Crush
        R("macro5", "reverb.mix", 35), R("macro5", "delay.mix", 30),          # Air
    ],
    ["Impact", "Boom", "Tail", "Crush", "Air", "", "", ""])
