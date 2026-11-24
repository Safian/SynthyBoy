# Batch 1, keys, plucks and the two sequenced presets. `add`, `R`, `pressure` and `limiter_last` are provided by factory-presets-batch1.py.


def steps(values, eps=0.0004):
    """The points of a stepped LFO shape: `values` (0 .. 1) held for equal parts of the cycle."""
    n = len(values)
    pts = []
    for i, v in enumerate(values):
        pts.append((i / n, v, 0.0))
        pts.append(((i + 1) / n - eps if i < n - 1 else 1.0, v, 0.0))
    return pts


add("Tine Glass Keys", "Keys", "electric-piano,fm,tine,velocity,tremolo,phaser,eq",
    "An electric piano from two sines (the second phase-modulates the first with an index that dies away) plus a bell-partial tine that pings on hard hits; velocity decides the bite, the wheel brings in an auto-pan tremolo",
    {
        "master.polyphony": 10,
        "osc1.on": 1, "osc1.wavetable": "Sine", "osc1.level": 72, "osc1.warp.type": "FM from Osc 2", "osc1.warp.amount": 12,
        "osc2.on": 1, "osc2.wavetable": "Sine", "osc2.level": 28, "osc2.destination": "None",
        "osc3.on": 1, "osc3.wavetable": "Bell Partials", "osc3.level": 0, "osc3.transpose": 24,
        "filter1.on": 1, "filter1.model": "Digital", "filter1.cutoff": 6500, "filter1.resonance": 8, "filter1.keytrack": 40,
        "env1.attack": 1, "env1.decay": 2800, "env1.sustain": 0, "env1.release": 380, "env1.decay_curve": -3,
        "env3.attack": 0, "env3.decay": 650, "env3.sustain": 0, "env3.release": 200,
        "env4.attack": 0, "env4.decay": 110, "env4.sustain": 0, "env4.release": 60,
        "lfo1.shape": "Sine", "lfo1.rate": 4.8, "lfo2.shape": "Sine", "lfo2.rate": 4.8,
        "phaser.on": 1, "phaser.mix": 16, "phaser.rate": 0.3, "phaser.depth": 22, "phaser.feedback": 35,
        "flanger.on": 1, "flanger.mix": 30, "flanger.rate": 0.22, "flanger.feedback": 35, "flanger.depth": 40,
        "eq.on": 1, "eq.mid.cutoff": 500, "eq.mid.gain": -2, "eq.high.cutoff": 7000, "eq.high.gain": 2.5,
        "reverb.on": 1, "reverb.mix": 14, "reverb.decay": 1800, "reverb.type": "Plate",
    },
    [
        R("env3", "osc1.warp.amount", 38),                                   # the bark of the attack dies into a soft tone
        R("env4", "osc3.level", 40),                                         # the tine
        R("velocity", "osc1.warp.amount", 40), R("velocity", "osc3.level", 30), R("velocity", "filter1.cutoff", 16),
        R("lfo1", "osc1.pan", 40, aux="mod_wheel"),                          # the wheel: auto-pan tremolo
        R("lfo2", "osc1.pan", 24, aux="poly_aftertouch"),                    # pressure on a held key: the tremolo also comes in
        R("macro1", "osc3.level", 55), R("macro1", "env4.decay", 30),        # Bell
        R("macro2", "osc1.warp.amount", 45), R("macro2", "osc2.level", 20),  # Bark
        R("macro3", "env1.decay", 40), R("macro3", "env3.decay", 30),        # Sustain
        R("macro4", "phaser.mix", 60), R("macro4", "flanger.mix", 70), R("macro4", "flanger.feedback", 25),        # Phase
        R("macro5", "filter1.cutoff", -45),                                  # Mellow
    ],
    ["Bell", "Bark", "Sustain", "Phase", "Mellow", "", "", ""])

add("Karplus Harp", "Pluck", "string,comb,physical,noise-burst,harp,keytracked",
    "A plucked string from the Comb filter model: a few milliseconds of noise from envelope 4 excite a comb that is keytracked to the note and rings for about a second; a second filter damps the highs as it fades and a limiter catches the pick",
    {
        "osc1.on": 0,
        "noise.on": 1, "noise.level": 0, "noise.destination": "Filter 1",
        "filter.routing": "Series 1 to 2",
        "filter1.on": 1, "filter1.model": "Comb", "filter1.cutoff": 261.6256, "filter1.resonance": 100, "filter1.keytrack": 100,
        "filter2.on": 1, "filter2.model": "Digital", "filter2.style": 1, "filter2.cutoff": 700, "filter2.resonance": 5,
        "env1.attack": 0, "env1.decay": 4500, "env1.sustain": 0, "env1.release": 250,
        "env2.attack": 0, "env2.decay": 800, "env2.sustain": 0, "env2.release": 200,
        "env4.attack": 0, "env4.decay": 16, "env4.sustain": 0, "env4.release": 10,
        "chorus.on": 1, "chorus.mix": 14, "chorus.rate": 0.3, "utility.on": 1, "utility.level": 10,
        "eq.on": 1, "eq.level": 12, "eq.low.cutoff": 90, "eq.low.gain": -4, "eq.mid.cutoff": 2500, "eq.mid.gain": 2,
        **limiter_last(-2),
        "reverb.on": 1, "reverb.mix": 18, "reverb.decay": 2200, "reverb.type": "Plate",
    },
    [
        R("env4", "noise.level", 75),                                       # the pluck
        R("env2", "filter2.cutoff", 18),                                     # the highs fade first
        R("velocity", "env4.decay", 5), R("velocity", "filter2.cutoff", 10), R("velocity", "noise.level", 20),
        R("mod_wheel", "env1.decay", -40),                                   # the wheel mutes the strings
        R("macro1", "filter2.cutoff", 40),                                   # Brightness
        R("macro2", "filter1.comb_offset", 45),                              # Ring Tone: the comb offset adds the upper partials and the ring
        R("macro3", "env4.decay", 9),                                        # Pick Softness
        R("macro4", "chorus.mix", 40), R("macro4", "reverb.mix", 30),        # Space
        R("macro5", "eq.low.gain", 70),                                      # Body
    ],
    ["Brightness", "Ring Tone", "Pick Softness", "Space", "Body", "", "", ""])

add("Ring Mod Gamelan", "Pluck", "ring-mod,bell,metal,inharmonic,gamelan,mallet",
    "Struck bars whose overtones are not harmonic: a triangle through the Ring Mod filter with a keytracked carrier, a bell-partial ping on top, and a Multi SVF low pass that closes as the sound dies",
    {
        "osc1.on": 1, "osc1.wavetable": "Triangle", "osc1.level": 62,
        "osc2.on": 1, "osc2.wavetable": "Bell Partials", "osc2.level": 22, "osc2.transpose": 12,
        "filter.routing": "Series 1 to 2",
        "filter1.on": 1, "filter1.model": "Ring Mod", "filter1.cutoff": 722, "filter1.resonance": 40, "filter1.keytrack": 100, "filter1.mix": 62,
        "filter2.on": 1, "filter2.model": "Multi SVF", "filter2.style": 2, "filter2.cutoff": 4200, "filter2.resonance": 18, "filter2.keytrack": 40,
        "env1.attack": 0, "env1.decay": 1500, "env1.sustain": 0, "env1.release": 320, "env1.decay_curve": -3,
        "env2.attack": 0, "env2.decay": 520, "env2.sustain": 0,
        "env3.attack": 0, "env3.decay": 240, "env3.sustain": 0,
        "delay.on": 1, "delay.sync": "Tempo dotted", "delay.division": 5, "delay.mix": 20, "delay.feedback": 36, "delay.style": "Ping pong",
        "reverb.on": 1, "reverb.mix": 20, "reverb.decay": 2600, "reverb.type": "Hall",
        "eq.on": 1, "eq.low.cutoff": 150, "eq.low.gain": -3, "eq.high.cutoff": 8000, "eq.high.gain": 1.5,
    },
    [
        R("env2", "filter2.cutoff", 26),                                     # the bar rings and darkens
        R("env3", "filter1.mix", 28),                                        # the strike is the most metallic moment
        R("velocity", "filter1.mix", 24), R("velocity", "filter2.cutoff", 18),
        R("mod_wheel", "filter1.mix", 30),
        R("macro1", "filter1.mix", 38),                                      # Metal
        R("macro2", "filter1.cutoff", 20),                                   # Inharmonic
        R("macro3", "env1.decay", 35), R("macro3", "env1.release", 25),      # Ring Time
        R("macro4", "delay.mix", 30), R("macro4", "reverb.mix", 30),         # Space
        R("macro5", "osc2.level", 45),                                       # Shimmer
    ],
    ["Metal", "Inharmonic", "Ring Time", "Space", "Shimmer", "", "", ""])

PENTA = [0, 7, 12, 7, 3, 10, 15, 12, 0, 3, 7, 10, 12, 15, 19, 12]            # semitones above the key: all inside the minor pentatonic scale
STEP_PITCH = {"lfo": 1, "name": "Penta Run", "smooth": False, "grid_x": 16, "grid_y": 24, "points": steps([s / 24.0 for s in PENTA])}

add("Pentatonic Step Pluck", "Pluck", "sequence,arpeggio,pentatonic,tempo-sync,drawn-lfo,transpose-snap",
    "Hold one key and it plays itself: a drawn 16-step LFO walks the oscillators through the minor pentatonic over one bar, each step plucked by a second tempo-synced LFO; the transpose snap keeps every step in the scale",
    {
        "osc1.on": 1, "osc1.wavetable": "Saw", "osc1.level": 0, "osc1.unison.voices": 2, "osc1.unison.detune": 9, "osc1.transpose_snap": 1193,
        "osc2.on": 1, "osc2.wavetable": "Pulse 25%", "osc2.level": 0, "osc2.transpose_snap": 1193, "osc2.transpose": 12,
        "filter1.on": 1, "filter1.model": "Analog", "filter1.cutoff": 600, "filter1.resonance": 28, "filter1.keytrack": 50,
        "env1.attack": 0, "env1.decay": 800, "env1.sustain": 100, "env1.release": 220,
        "lfo1.shape": "Custom", "lfo1.sync": "Tempo", "lfo1.division": "1 bar", "lfo1.smooth": 0,
        "lfo2.shape": "Exp Down", "lfo2.sync": "Tempo", "lfo2.division": "1/16 note", "lfo2.smooth": 0,
        "delay.on": 1, "delay.sync": "Tempo dotted", "delay.division": 5, "delay.mix": 24, "delay.feedback": 42, "delay.style": "Ping pong",
        "reverb.on": 1, "reverb.mix": 16, "reverb.decay": 1900, "reverb.type": "Plate",
        "chorus.on": 1, "chorus.mix": 12,
        **limiter_last(-8),
    },
    [
        R("lfo1", "osc1.transpose", 25), R("lfo1", "osc2.transpose", 25),    # the walk (24 semitones for the highest step)
        R("lfo2", "osc1.level", 78), R("lfo2", "osc2.level", 55),            # every step is a pluck
        R("lfo2", "filter1.cutoff", 24),
        R("velocity", "filter1.cutoff", 18), R("velocity", "osc2.level", 20),
        R("mod_wheel", "osc1.level", 24),                                    # the wheel holds the notes between the plucks
        R("macro1", "filter1.cutoff", 45),                                   # Bright
        R("macro2", "osc1.level", 22), R("macro2", "osc2.level", 18),        # Hold
        R("macro3", "filter1.resonance", 40),                                # Squelch
        R("macro4", "delay.mix", 35), R("macro4", "reverb.mix", 25),         # Echo
        R("macro5", "osc1.unison.detune", 70), R("macro5", "osc1.unison.voices", 20),   # Fat
    ],
    ["Bright", "Hold", "Squelch", "Echo", "Fat", "", "", ""],
    {"lfo_shapes": [STEP_PITCH]})

GATE = [1, 0.0, 1, 1, 0.0, 1, 0.5, 1, 1, 0.0, 1, 0.0, 1, 1, 0.0, 1]
GATE_SHAPE = {"lfo": 1, "name": "Gate 16", "smooth": False, "grid_x": 16, "grid_y": 4, "points": steps(GATE)}

add("Tempo Gate Chords", "Keys", "gate,trance,tempo-sync,drawn-lfo,sidechain,chords",
    "Play a chord and it is chopped into a drawn 16-step gate that runs with the tempo, with a quarter-note filter pump under it; the Smooth macro takes the gate away again and leaves a pad",
    {
        "osc1.on": 1, "osc1.wavetable": "Saw", "osc1.level": 0, "osc1.unison.voices": 3, "osc1.unison.detune": 16, "osc1.unison.spread": 80,
        "osc2.on": 1, "osc2.wavetable": "Saw", "osc2.level": 0, "osc2.transpose": 12, "osc2.tune": 7,
        "sub.on": 1, "sub.waveform": "Triangle", "sub.octave": -1, "sub.level": 0,
        "filter1.on": 1, "filter1.model": "Digital", "filter1.cutoff": 900, "filter1.resonance": 24, "filter1.keytrack": 40,
        "env1.attack": 2, "env1.decay": 800, "env1.sustain": 100, "env1.release": 140,
        "lfo1.shape": "Custom", "lfo1.sync": "Tempo", "lfo1.division": "1 bar", "lfo1.smooth": 0,
        "lfo2.shape": "Exp Up", "lfo2.sync": "Tempo", "lfo2.division": "1/4 note", "lfo2.smooth": 0,
        "delay.on": 1, "delay.sync": "Tempo dotted", "delay.division": 5, "delay.mix": 22, "delay.feedback": 38,
        "reverb.on": 1, "reverb.mix": 16, "reverb.decay": 1700,
        "chorus.on": 1, "chorus.mix": 14,
    },
    [
        R("lfo1", "osc1.level", 62, label="gate1"), R("lfo1", "osc2.level", 46, label="gate2"), R("lfo1", "sub.level", 40, label="gate3"),     # the gate
        R("lfo2", "filter1.cutoff", 28, label="pump"),                                                  # the pump: the filter opens after every beat
        R("velocity", "filter1.cutoff", 14),
        pressure(18),
        R("mod_wheel", "filter1.resonance", 30),
        R("macro1", "osc1.level", 38), R("macro1", "osc2.level", 30), R("macro1", "sub.level", 20),     # Smooth: the level that the gate leaves open
        R("macro2", "@pump.amount", 40), R("macro2", "filter1.cutoff", -8), R("macro2", "filter1.resonance", 20),     # Pump: a lower closed filter, a higher peak
        R("macro3", "filter1.cutoff", 35),                                                              # Bright
        R("macro4", "delay.mix", 35), R("macro4", "reverb.mix", 25),                                    # Echo
        R("macro5", "osc1.unison.detune", 40),                                                          # Fat
    ],
    ["Smooth", "Pump", "Bright", "Echo", "Fat", "", "", ""],
    {"lfo_shapes": [GATE_SHAPE]})
